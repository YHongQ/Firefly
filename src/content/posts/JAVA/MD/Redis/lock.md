---
title: Redis黑马课程学习:秒杀优惠券一人一单——Spring 事务与锁的巧妙配合
published: 2026-05-28
tags: [Redis, 黑马课程,学习笔记,JAVA,Spring,事务,锁]
pinned: true
category: 学习笔记
draft: false
---

# 秒杀优惠券一人一单——Spring 事务与锁的巧妙配合

在高并发秒杀场景中，实现“一人一单”且保证库存安全扣减，是常见但容易出错的需求。本文通过一段实际代码，剖析如何在 Spring 中结合 `synchronized` 锁、`AopContext.currentProxy()` 以及 `@Transactional` 事务，完成这一逻辑。

---

## 1. 背景与设计初衷

秒杀业务有两个核心难题：

- **库存超卖**：多线程同时扣减库存，必须保证原子性。
- **一人一单**：同一个用户不能重复购买同一优惠券，需要在并发下准确判断。

我们很容易想到：扣库存和创建订单应该是一个事务，而判断用户是否已购买和扣库存需要加锁。但如果在业务方法上直接加锁并调用本类的另一个带事务的方法，会遇到 **Spring AOP 自调用事务失效** 的问题。

> Spring AOP 自调用事务失效问题：
> 
> - 直接调用本类的事务方法，如 `this.createOrder()`，事务注解失效。

> **原因：** 直接调用时，是目标对象的方法，不会经过代理，事务注解失效。通过代理对象调用时，是代理对象的方法，会经过代理，事务注解生效。

因此，这段代码的设计逻辑围绕以下要点展开：

- 使用 `synchronized` 对用户 ID 加锁，保证同一用户的请求串行执行。
- 通过 **代理对象** 调用事务方法，让 `@Transactional` 生效。
- 在事务中完成“判断是否已购买 -> 扣库存 -> 创建订单”的原子操作。

---

## 2. 代码整体流程

1. 校验秒杀优惠券是否存在，是否在有效期内，库存是否大于 0。
2. 获取当前登录用户 ID，对 `userID.toString().intern()` 加锁（保证同一用户串行）。
3. 通过 `AopContext.currentProxy()` 获取当前类的代理对象，调用 `createOrder()` 方法。
4. 在 `createOrder()` 中（此时事务已生效）：
   - 再次校验该用户是否已有订单。
   - 使用乐观锁思想更新库存：`setSql("stock = stock - 1")...gt("stock", 0)`。
   - 生成全局唯一订单 ID，保存订单信息。
5. 返回订单 ID 或错误提示。

---

## 3. 设计要点分析

### 3.1 为什么使用 `AopContext.currentProxy()`？

`createOrder()` 方法被 `@Transactional` 标注，Spring 通过 AOP 生成代理对象来管理事务。**在同一类中直接通过 `this.createOrder()` 调用时，调用的是目标对象的方法，不会经过代理，因此事务注解失效。**

``` java

// 错误示例：事务不会生效
public Result seckillVoucher(Long voucherId) {
    // ...
    synchronized (userID.toString().intern()) {
        return this.createOrder(voucherId); // 直接调用，无事务
    }
}

```

正确做法是获取当前的 AOP 代理，用代理对象调用：

```java
IVoucherOrderService proxy = (IVoucherOrderService) AopContext.currentProxy();
return proxy.createOrder(voucherId);
```

**前提**：需要在启动类或配置类上添加 `@EnableAspectJAutoProxy(exposeProxy = true)` 暴露代理对象，否则会抛出异常。

### 3.2 事务设计原因

`createOrder()` 内的三个操作必须在一个事务中完成：

1. **查询该用户是否已有此优惠券的订单**。
2. **扣减库存**（采用 `stock > 0` 条件防止超卖）。
3. **保存订单**。

任何一步失败，整个事务回滚，保证数据一致性。例如：扣库存成功后，保存订单时出现异常，库存会回滚，不会出现“扣了库存却没生成订单”的情况。

### 3.3 锁的设计

```java
synchronized (userID.toString().intern()) {
    // ...
}
```

**为什么锁对象是 `userID.toString().intern()`？**

- `userID` 是 `Long` 类型，直接 `synchronized (userID)` 会因为自动装箱生成不同对象，导致锁失效。
- `userID.toString()` 每次生成新的字符串对象，也不能作为锁。
- `.intern()` 保证同一个内容的字符串在常量池中是同一个对象，**相同的用户 ID 会竞争同一把锁**。
- 锁的粒度是“用户级”，不同用户之间不互相阻塞，兼顾并发性能与安全性。

**为什么在锁块内还要再查一次订单？**

外层的库存和秒杀时间判断是无锁的，多个线程可能同时通过前置检查进入锁等待。第一个线程创建订单后，后续等待的线程再执行业务时，必须再次检查该用户是否已购买，否则可能产生重复订单。这一层二次检查（类似单例模式的双重检查）正是锁存在的意义。

---

## 4. 小结

- **事务**保证了“扣库存”与“创建订单”的原子性。
- **AOP 代理**解决了同一类内事务自调用失效的问题。
- **对用户 ID 加锁**实现了一人一单且用户间并发的串行化，配合事务内的重复查询防止重复下单。
- **库存更新用乐观锁** (`gt("stock", 0)`) 进一步避免超卖。

整套设计环环相扣，是学习 Spring 事务和并发控制的经典案例。

---

## 5. 源码附上

```java
package com.hmdp.service.impl;

import com.hmdp.dto.Result;
import com.hmdp.entity.SeckillVoucher;
import com.hmdp.entity.VoucherOrder;
import com.hmdp.mapper.VoucherOrderMapper;
import com.hmdp.service.ISeckillVoucherService;
import com.hmdp.service.IVoucherOrderService;
import com.baomidou.mybatisplus.extension.service.impl.ServiceImpl;
import com.hmdp.utils.RedisIdWorker;
import com.hmdp.utils.UserHolder;
import org.springframework.aop.framework.AopContext;
import org.springframework.data.redis.core.StringRedisTemplate;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import javax.annotation.Resource;
import java.time.LocalDateTime;

@Service
public class VoucherOrderServiceImpl
        extends ServiceImpl<VoucherOrderMapper, VoucherOrder>
        implements IVoucherOrderService {

    @Resource
    private StringRedisTemplate stringRedisTemplate;

    @Resource
    private RedisIdWorker redisIdWorker;

    @Resource
    private ISeckillVoucherService seckillVoucherService;

    @Override
    public Result seckillVoucher(Long voucherId) {
        // 1. 查询秒杀优惠券
        SeckillVoucher seckillVoucher = seckillVoucherService.getById(voucherId);
        if (seckillVoucher == null) {
            return Result.fail("优惠券不存在");
        }
        Integer stock = seckillVoucher.getStock();
        LocalDateTime startTime = seckillVoucher.getBeginTime();
        LocalDateTime endTime = seckillVoucher.getEndTime();
        LocalDateTime now = LocalDateTime.now();

        // 2. 判断秒杀是否开始
        if (now.isBefore(startTime)) {
            return Result.fail("秒杀优惠券未开始");
        }
        // 3. 判断秒杀是否结束
        if (now.isAfter(endTime)) {
            return Result.fail("秒杀优惠券已结束");
        }
        // 4. 判断库存
        if (stock <= 0) {
            return Result.fail("秒杀优惠券已售罄");
        }

        // 一人一单：对用户ID加锁
        Long userId = UserHolder.getUser().getId();
        synchronized (userId.toString().intern()) {
            // 获取代理对象，让事务生效
            IVoucherOrderService proxy =
                    (IVoucherOrderService) AopContext.currentProxy();
            return proxy.createOrder(voucherId);
        }
    }

    @Transactional
    public Result createOrder(Long voucherId) {
        Long userId = UserHolder.getUser().getId();

        // 双重检查：防止锁内重复下单
        int count = query()
                .eq("user_id", userId)
                .eq("voucher_id", voucherId)
                .count();
        if (count > 0) {
            return Result.fail("用户已购买过该优惠券");
        }

        // 5. 扣减库存（乐观锁）
        boolean result = seckillVoucherService.update()
                .setSql("stock = stock - 1")
                .eq("voucher_id", voucherId)
                .gt("stock", 0)
                .update();
        if (!result) {
            return Result.fail("秒杀优惠券已售罄");
        }

        // 6. 创建订单
        Long orderId = redisIdWorker.nextId("order");
        VoucherOrder voucherOrder = new VoucherOrder();
        voucherOrder.setId(orderId);
        voucherOrder.setUserId(userId);
        voucherOrder.setVoucherId(voucherId);
        save(voucherOrder);

        return Result.ok(orderId);
    }
}
```

---

> **提示**：使用该代码时，请确保配置类上添加了 `@EnableAspectJAutoProxy(exposeProxy = true)`，并且 `UserHolder` 能正确获取当前线程的用户信息。

---

希望这篇博客能帮助你理解 Spring 中事务、代理与锁协同工作的精妙之处。如有疑问，欢迎交流讨论！


