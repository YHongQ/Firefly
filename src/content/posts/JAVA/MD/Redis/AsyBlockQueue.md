---
title: Redis黑马点评：秒杀券异步操作：从零理解消息队列与Lua脚本的完美结合
published: 2026-06-04
tags: [Redis, 消息队列, Lua脚本, 异步处理, 线程池, JAVA]
pinned: true
category: 学习笔记
draft: false
---

# Redis秒杀券异步操作：从零理解消息队列与Lua脚本的完美结合

在高并发秒杀场景中，如何既保证系统的高性能，又确保数据的一致性？今天我们从学习者的角度，深入剖析一个完整的秒杀券异步处理方案，看看Redis、Lua脚本、消息队列、线程池等技术是如何协同工作的。

---

## 1. 为什么需要异步处理？

在秒杀场景中，如果所有操作都在主线程同步完成，会面临以下问题：

- **响应慢**：每个请求都需要等待数据库操作完成，用户体验差
- **系统压力大**：数据库连接被长时间占用，容易达到瓶颈
- **并发能力有限**：同步处理限制了系统的吞吐量

**异步处理的思路**：主线程只做快速判断和入队操作，立即返回响应；订单创建等耗时操作由后台线程池慢慢处理。

---

## 2. 整体架构设计

```
用户请求 → Lua脚本原子判断 → 入队 → 立即返回
                    ↓
              阻塞队列（消息队列）
                    ↓
              线程池（N个线程）
                    ↓
          分布式锁 + 数据库事务操作
```

这个设计的关键点：
- **Redis Lua脚本**：保证库存判断和订单记录的原子性
- **阻塞队列**：作为消息队列，缓冲请求
- **线程池**：异步处理订单创建
- **分布式锁**：保证一人一单

---

## 3. Lua脚本：Redis原子操作的守护者

### 3.1 为什么需要Lua脚本？

在秒杀场景中，我们需要同时做两件事：
1. 判断库存是否充足
2. 判断用户是否已经下过单

如果用普通的Redis命令分两步执行，在并发情况下会出现问题：
- 线程A检查库存充足
- 线程B也检查库存充足
- 两个线程都扣减库存
- 结果库存变成负数，超卖！

**Lua脚本的优势**：Redis保证Lua脚本的执行是原子的，要么全部执行，要么都不执行。

### 3.2 Lua脚本实现解析

```lua
-- 获取优惠券ID和用户ID
local voucherID = ARGV[1]
local userID = ARGV[2]

-- 构造Redis key
local stockKey = 'seckill:stock:' .. voucherID
local orderKey = 'seckill:order:' .. voucherID

-- 1. 判断库存是否充足
local stock = tonumber(redis.call('GET', stockKey))
if stock == nil or stock <= 0 then
    return 1  -- 库存不足
end

-- 2. 判断用户是否已经下单（一人一单）
if(redis.call('sismember', orderKey, userID) == 1) then
    return 2  -- 用户已下单
end

-- 3. 库存充足且用户未下单，执行原子操作
redis.call('incrby', stockKey, -1)  -- 库存减1
redis.call('sadd', orderKey, userID)  -- 添加到订单集合
return 0  -- 成功
```

**技术要点**：
- **返回值设计**：0成功，1库存不足，2重复下单，便于Java代码判断
- **Set结构**：`sismember`和`sadd`操作天然保证一人一单
- **原子性**：整个脚本在Redis中单线程执行，不会出现并发问题

### 3.3 Java中调用Lua脚本

```java
// 静态初始化Lua脚本
private static final DefaultRedisScript<Long> SECKILL_SCRIPT;
static {
    SECKILL_SCRIPT = new DefaultRedisScript<>();
    SECKILL_SCRIPT.setLocation(new ClassPathResource("seckill.lua"));
    SECKILL_SCRIPT.setResultType(Long.class);
}

// 执行Lua脚本
Long result = stringRedisTemplate.execute(
    SECKILL_SCRIPT,
    Collections.emptyList(),  // 没有KEYS参数
    voucherId.toString(),      // ARGV[1]
    userID.toString()          // ARGV[2]
);
```

---

## 4. 消息队列：缓冲压力的利器

### 4.1 为什么选择BlockingQueue？

Java的`BlockingQueue`是一个线程安全的阻塞队列，适合作为内存消息队：
---
> 缺点也很明显：存储在内存，当Java进程重启或关闭时，队列中的数据会丢失。如果需要持久化存储，需要额外的配置。
> 
> 并且内存容量有限，不能无限增长。会因为内存溢出而导致系统崩溃。所以，需要根据实际情况选择合适的队列容量。

```java
// 创建容量为1024*1024的阻塞队列
private BlockingQueue<VoucherOrder> voucherOrderBlockingQueue = 
    new ArrayBlockingQueue<VoucherOrder>(1024 * 1024);
```

**BlockingQueue的特点**：
- **线程安全**：多个线程可以安全地并发操作
- **阻塞机制**：队列满时，`put()`会阻塞；队列空时，`take()`会阻塞
- **容量限制**：防止内存溢出，超过容量会阻塞

### 4.2 入队操作

```java
// 创建订单对象
Long orderID = redisIdWorker.nextId("order");
VoucherOrder voucherOrder = new VoucherOrder();
voucherOrder.setId(orderID);
voucherOrder.setUserId(userID);
voucherOrder.setVoucherId(voucherId);

// 放入队列（如果队列满会阻塞）
voucherOrderBlockingQueue.add(voucherOrder);
```

**设计考虑**：使用`add()`而不是`put()`，如果队列满会抛出异常，避免无限等待。

---

## 5. 线程池：异步处理的核心

### 5.1 线程池的创建与初始化

```java
// 创建固定大小为10的线程池
private static final ExecutorService SECKILL_EXECUTOR_SERVICE = 
    Executors.newFixedThreadPool(10);

// 项目启动时初始化
@PostConstruct
private void init() {
    SECKILL_EXECUTOR_SERVICE.submit(new VocherOrderHandler());
}
```

**技术要点**：
- **@PostConstruct**：在Spring Bean初始化完成后自动执行。 因为当前任务是异步处理， 所以需要提前启动线程池，确保第一个请求到来时线程池已就绪。
- **固定大小线程池**：10个线程处理订单，避免资源耗尽
- **提前启动**：确保第一个请求到来时线程池已就绪

### 5.2 Runnable任务：订单处理器

```java
private class VocherOrderHandler implements Runnable {
    @Override
    public void run() {
        while (true) {
            try {
                // 从队列中取出订单（如果队列空会阻塞）
                VoucherOrder voucherOrder = voucherOrderBlockingQueue.take();
                // 处理订单
                handlerVoucherOrder(voucherOrder);
            } catch (InterruptedException e) {
                log.error("订单处理线程被中断", e);
            }
        }
    }
}
```


- **阻塞等待**：`take()`方法在队列空时会阻塞，不浪费CPU
- **异常处理**：捕获中断异常，保证线程不会意外退出

---

## 6. 分布式锁：跨JVM的一人一单保证

### 6.1 为什么需要分布式锁？

在多服务器部署的情况下，`synchronized`只能保证单个JVM内的线程安全，无法解决跨服务器的一人一单问题。

**Redisson分布式锁**：基于Redis实现的分布式锁，支持锁续命、可重入等特性。

### 6.2 分布式锁的使用

```java
private void handlerVoucherOrder(VoucherOrder voucherOrder) {
    Long userID = voucherOrder.getUserId();
    
    // 1. 创建锁对象（按用户ID加锁）
    RLock lock = redissonClient.getLock("lock:order:" + userID);
    
    // 2. 尝试获取锁
    boolean locked = lock.tryLock();
    if (!locked) {
        log.error("用户{"+userID+"}下单失败，获取锁失败");
        return;
    }
    
    // 3. 执行业务逻辑
    try {
        proxy.createOrder(voucherOrder);
    } catch (Throwable e) {
        log.error("用户{"+userID+"}下单失败，创建订单失败", e);
    } finally {
        // 4. 释放锁
        lock.unlock();
    }
}
```

**技术要点**：
- **锁粒度**：按用户ID加锁，不同用户互不影响
- **tryLock()**：非阻塞获取锁，失败立即返回
- **finally释放**：确保锁一定会被释放，避免死锁

---

## 7. Spring事务：数据一致性的最后保障

### 7.1 事务方法设计

```java
@Transactional
public void createOrder(VoucherOrder voucherOrder) {
    Long userID = voucherOrder.getUserId();
    Long voucherId = voucherOrder.getVoucherId();

    // 1. 再次检查一人一单（双重检查）
    int count = query()
        .eq("user_id", userID)
        .eq("voucher_id", voucherId)
        .count();
    if (count > 0) {
        log.error("用户{"+userID+"}已购买过该优惠券");
        return;
    }

    // 2. 扣减库存（乐观锁）
    boolean result = seckillVoucherService.update()
        .setSql("stock = stock - 1")
        .eq("voucher_id", voucherId)
        .gt("stock", 0)  // 乐观锁：库存大于0才更新
        .update();
    if (!result) {
        log.error("用户{"+userID+"}下单失败，优惠券已售罄");
        return;
    }

    // 3. 保存订单
    save(voucherOrder);
}
```

**技术要点**：
- **@Transactional**：保证数据库操作的原子性
- **双重检查**：虽然Redis Lua脚本已经检查过，但数据库层再检查一次更安全
- **乐观锁**：`gt("stock", 0)`防止数据库层面的超卖
- **事务回滚**：任何一步失败，整个操作回滚

### 7.2 为什么需要AOP代理？

```java
// 获取代理对象
proxy = (IVoucherOrderService) AopContext.currentProxy();
```

**原因**：`createOrder`方法有`@Transactional`注解，需要通过代理对象调用才能让事务生效。

**Spring AOP原理**：
- Spring为Bean创建代理对象
- 代理对象在方法调用前后织入事务逻辑
- 直接调用`this.createOrder()`不会经过代理，事务失效

---

## 8. 完整流程梳理

### 8.1 主线程流程（快速响应）

```
1. 用户发起秒杀请求
2. 执行Lua脚本：
   - 检查库存
   - 检查是否重复下单
   - 原子扣库存+记录订单
3. 创建订单对象
4. 放入阻塞队列
5. 立即返回订单ID给用户
```

**时间复杂度**：O(1)，只涉及Redis操作，毫秒级响应。

### 8.2 后台线程流程（异步处理）

```
1. 线程池中的线程从队列取出订单
2. 获取分布式锁（按用户ID）
3. 调用createOrder方法（事务生效）：
   - 检查数据库中是否已下单
   - 扣减数据库库存（乐观锁）
   - 保存订单记录
4. 释放分布式锁
```

**特点**：不阻塞用户请求，慢慢处理数据库操作。



## 11. 完整源码

```java
package com.hmdp.service.impl;

import lombok.extern.slf4j.Slf4j;
import org.springframework.core.io.ClassPathResource;
import com.hmdp.dto.Result;
import com.hmdp.entity.VoucherOrder;
import com.hmdp.mapper.VoucherOrderMapper;
import com.hmdp.service.ISeckillVoucherService;
import com.hmdp.service.IVoucherOrderService;
import com.baomidou.mybatisplus.extension.service.impl.ServiceImpl;
import com.hmdp.utils.RedisIdWorker;
import com.hmdp.utils.UserHolder;
import org.redisson.api.RLock;
import org.redisson.api.RedissonClient;
import org.springframework.aop.framework.AopContext;
import org.springframework.data.redis.core.StringRedisTemplate;
import org.springframework.data.redis.core.script.DefaultRedisScript;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import javax.annotation.PostConstruct;
import javax.annotation.Resource;
import java.util.Collections;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

@Slf4j
@Service
public class VoucherOrderServiceImpl extends ServiceImpl<VoucherOrderMapper, VoucherOrder> 
        implements IVoucherOrderService {
    
    @Resource
    private StringRedisTemplate stringRedisTemplate;

    @Resource
    private RedisIdWorker redisIdWorker;

    @Resource
    private ISeckillVoucherService seckillVoucherService;

    @Resource
    private RedissonClient redissonClient;

    // Lua脚本
    private static final DefaultRedisScript<Long> SECKILL_SCRIPT;
    static {
        SECKILL_SCRIPT = new DefaultRedisScript<>();
        SECKILL_SCRIPT.setLocation(new ClassPathResource("seckill.lua"));
        SECKILL_SCRIPT.setResultType(Long.class);
    }

    // 阻塞队列（消息队列）
    private BlockingQueue<VoucherOrder> voucherOrderBlockingQueue = 
        new ArrayBlockingQueue<VoucherOrder>(1024 * 1024);

    // 线程池
    private static final ExecutorService SECKILL_EXECUTOR_SERVICE = 
        Executors.newFixedThreadPool(10);

    // 初始化
    @PostConstruct
    private void init() {
        SECKILL_EXECUTOR_SERVICE.submit(new VocherOrderHandler());
    }

    // 订单处理线程
    private class VocherOrderHandler implements Runnable {
        @Override
        public void run() {
            while (true) {
                try {
                    VoucherOrder voucherOrder = voucherOrderBlockingQueue.take();
                    handlerVoucherOrder(voucherOrder);
                } catch (InterruptedException e) {
                    log.error("订单处理线程被中断", e);
                }
            }
        }
    }

    // 代理对象
    private IVoucherOrderService proxy;

    // 处理订单
    private void handlerVoucherOrder(VoucherOrder voucherOrder) {
        Long userID = voucherOrder.getUserId();
        RLock lock = redissonClient.getLock("lock:order:" + userID);
        boolean locked = lock.tryLock();
        if (!locked) {
            log.error("用户{"+userID+"}下单失败，获取锁失败");
            return;
        }
        try {
            proxy.createOrder(voucherOrder);
        } catch (Throwable e) {
            log.error("用户{"+userID+"}下单失败，创建订单失败", e);
        } finally {
            lock.unlock();
        }
    }

    // 秒杀入口
    @Override
    public Result seckillVoucher(Long voucherId) {
        Long userID = UserHolder.getUser().getId();
        
        // 执行Lua脚本
        Long result = stringRedisTemplate.execute(
            SECKILL_SCRIPT,
            Collections.emptyList(),
            voucherId.toString(),
            userID.toString()
        );
        
        int r = result.intValue();
        if (r == 1) {
            return Result.fail("库存不足");
        }
        if (r == 2) {
            return Result.fail("您已下单过");
        }

        // 创建订单并放入队列
        Long orderID = redisIdWorker.nextId("order");
        VoucherOrder voucherOrder = new VoucherOrder();
        voucherOrder.setId(orderID);
        voucherOrder.setUserId(userID);
        voucherOrder.setVoucherId(voucherId);
        voucherOrderBlockingQueue.add(voucherOrder);

        proxy = (IVoucherOrderService) AopContext.currentProxy();
        return Result.ok(orderID);
    }

    // 创建订单（事务方法）
    @Transactional
    public void createOrder(VoucherOrder voucherOrder) {
        Long userID = voucherOrder.getUserId();
        Long voucherId = voucherOrder.getVoucherId();

        // 检查是否已下单
        int count = query()
            .eq("user_id", userID)
            .eq("voucher_id", voucherId)
            .count();
        if (count > 0) {
            log.error("用户{"+userID+"}已购买过该优惠券");
            return;
        }

        // 扣减库存（乐观锁）
        boolean result = seckillVoucherService.update()
            .setSql("stock = stock - 1")
            .eq("voucher_id", voucherId)
            .gt("stock", 0)
            .update();
        if (!result) {
            log.error("用户{"+userID+"}下单失败，优惠券已售罄");
            return;
        }

        // 保存订单
        save(voucherOrder);
    }
}
```

**Lua脚本（seckill.lua）**：

```lua
local voucherID = ARGV[1]
local userID = ARGV[2]

local stockKey = 'seckill:stock:' .. voucherID
local orderKey = 'seckill:order:' .. voucherID

local stock = tonumber(redis.call('GET', stockKey))
if stock == nil or stock <= 0 then
    return 1
end

if(redis.call('sismember', orderKey, userID) == 1) then
    return 2
end

redis.call('incrby', stockKey, -1)
redis.call('sadd', orderKey, userID)
return 0
```



---

希望这篇博客能帮助你理解秒杀系统的设计原理和实现细节！