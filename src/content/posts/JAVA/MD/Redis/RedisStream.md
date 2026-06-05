---
title: Redis黑马点评：秒杀券与Redis消息队列操作
published: 2026-06-04
tags: [Redis, 消息队列, Lua脚本, 异步处理, 线程池, JAVA]
pinned: true
category: 学习笔记
draft: false
---
# Redis Stream 消息队列实现异步秒杀——Lua 脚本 + 消费者组实战

上一篇文章我们讨论了用 `synchronized` + Spring 事务实现「一人一单」秒杀。那个方案能跑，但有一个明显的短板：**用户得等着服务器把订单写进数据库，接口才返回结果。** 当秒杀流量冲上来，数据库写入这个慢操作会堵住大量 Tomcat 线程，整个系统响应越来越慢。

怎么解决？答案是 **把下单动作异步化**。用户请求过来，服务器只做最核心的资格校验（库存够不够、有没有重复下单），校验通过后扔一条消息到队列，立刻告诉用户"抢到了"。真正的数据库写入交给后台线程慢慢消化。

用什么当消息队列？RabbitMQ、RocketMQ、Kafka 都能干，但这套项目本身强依赖 Redis，再引入一个中间件太重了。正好 Redis 5.0 之后自带了 **Stream** 数据结构，天生就是个轻量级消息队列。不需要额外部署，直接用。

[实战篇-11.RedisStream消息队列-实现异步秒杀_哔哩哔哩_bilibili](https://www.bilibili.com/video/BV1cr4y1671t?vd_source=3495715132f8b407fde241a86af24789&spm_id_from=333.788.player.switch&p=58)

---

## 1. Redis Stream 是什么？

用一句话理解：**Redis Stream 就是 Redis 内置的一个链表，但它比 List 多了消费者组、消息确认、消息回溯这些正经消息队列才有的能力。**

先看几个核心概念，不用记，跟着代码走一遍自然就懂了：

| 概念                                 | 大白话解释                                                     |
| ------------------------------------ | -------------------------------------------------------------- |
| **Stream**                     | 消息队列本身，一条 Stream 就是一个队列，比如 `stream.orders` |
| **消息**                       | 队列里的每一条数据，由若干键值对组成                           |
| **消费者组（Consumer Group）** | 一组消费者共享消费进度。同一条消息组内只会被一个消费者处理     |
| **消费者（Consumer）**         | 组里的一个具体干活的人，比如 `c1`                            |
| **XADD**                       | 往 Stream 尾部追加一条消息                                     |
| **XREADGROUP**                 | 消费者组模式读消息，读完后消息会进入 Pending 列表              |
| **XACK**                       | 消息处理完了，确认一下，从 Pending 列表移除                    |
| **Pending 列表**               | 被读走但还没确认的消息暂存区，消费者挂了可以从这里捞回来重试   |

整个流程画出来就三步：

```
用户请求 → Lua脚本(XADD入队) → 返回"抢到了"
                                    ↓
                         后台线程(XREADGROUP读消息 → 写数据库 → XACK确认)
```

---

## 2. 为什么要把资格校验写在 Lua 里？

资格校验要做三件事：**查库存 → 查是否重复下单 → 扣库存 + 标记已下单**。这三步必须是原子操作。

假设不用 Lua，用 Java 代码一步步调 Redis：

```java
// 危险写法：三步分开，中间可能被其他线程插入
String stock = redis.get("seckill:stock:10");  // 读到库存=1
Boolean exists = redis.sismember("seckill:order:10", userId); // 没下过单
// 此时另一个线程也读到了库存=1，也判断没下过单
redis.decr("seckill:stock:10");    // 两个线程都执行了扣减 → 超卖
redis.sadd("seckill:order:10", userId);
```

三步之间有空隙，并发下必然出问题。而 **Lua 脚本在 Redis 服务端执行，整个脚本执行期间 Redis 是单线程的，天然原子性**。这正是我们要的效果。

---

## 3. seckill.lua 逐行拆解

```lua
-- 从 Java 传过来的三个参数
local voucherId = ARGV[1]   -- 优惠券ID
local userId = ARGV[2]      -- 用户ID
local orderId = ARGV[3]     -- 预生成的订单ID

-- Redis 中的两个 Key
local stockKey = 'seckill:stock:' .. voucherId   -- 库存，比如 seckill:stock:10
local orderKey = 'seckill:order:' .. voucherId   -- 已下单用户集合（Set）

-- 第一步：查库存
local stock = tonumber(redis.call('GET', stockKey))
if stock == nil or stock <= 0 then
    return 1   -- 返回值 1：库存不足
end

-- 第二步：查是否重复下单（一人一单）
if(redis.call('sismember', orderKey, userId) == 1) then
    return 2   -- 返回值 2：已经下过单了
end

-- 第三步：扣库存 + 记录用户 + 发消息，一气呵成
redis.call('incrby', stockKey, -1)      -- 库存 -1
redis.call('sadd', orderKey, userId)     -- 用户加入已购集合

-- 核心：往 Redis Stream 里塞一条消息，* 表示让 Redis 自动生成消息 ID
redis.call('XADD', 'stream.orders', '*',
    'userId', userId,
    'id', orderId,
    'voucherId', voucherId)
return 0   -- 返回值 0：一切顺利
```

返回值约定：`0` 抢到、`1` 库存没了、`2` 已经买过了。Java 代码拿到返回值就知道该给用户什么提示。

> **知识点**：`redis.call('XADD', key, 'MAXLEN', '~', 10000, '*', ...)` 可以给 Stream 设置一个最大长度，超过时自动裁剪老消息，防止 Redis 内存无限增长。当前示例为了简洁没加，实际生产建议加上。

---

## 4. Java 端：请求入口——快进快出

用户请求到了 Controller，最终进入 `VoucherOrderServiceImpl.seckillVoucher()`。这个方法的逻辑非常简单：

```java
@Override
public Result seckillVoucher(Long voucherId) {
    Long userID = UserHolder.getUser().getId();
    Long orderID = redisIdWorker.nextId("order");  // 全局唯一ID生成器

    // 执行 Lua 脚本，返回值 0/1/2
    Long result = stringRedisTemplate.execute(
            SECKILL_SCRIPT,
            Collections.emptyList(),     // KEYS 为空，参数全放 ARGV
            voucherId.toString(),
            userID.toString(),
            String.valueOf(orderID));

    int r = result.intValue();
    if (r == 1) return Result.fail("库存不足");
    if (r == 2) return Result.fail("您已下单过");

    // 关键：获取代理对象，供子线程使用（Spring 事务需要通过代理调用）
    proxy = (IVoucherOrderService) AopContext.currentProxy();
    return Result.ok(orderID);    // 立刻返回，用户不用等数据库写入
}
```

用户看到 `Result.ok` 就说明抢到了，后续的数据库操作对他完全透明。整个接口耗时就是一次 Lua 脚本的网络往返，毫秒级。

---

## 5. 消费者：后台线程怎么消化消息

消息扔进 Stream 只是第一步，谁来消费它？答案是**项目启动时就拉起一个后台线程，死循环从 Stream 里读消息**。

### 5.1 线程启动时机

```java
private static final ExecutorService SECKILL_EXECUTOR_SERVICE =
        Executors.newFixedThreadPool(10);

@PostConstruct       // Spring Bean 初始化完成后自动执行
private void init() {
    SECKILL_EXECUTOR_SERVICE.submit(new VocherOrderHandler());
}
```

`@PostConstruct` 保证 Bean 一初始化，消费线程就开始跑，不用等第一个用户请求。

### 5.2 消息拉取：XREADGROUP

```java
List<MapRecord<String, Object, Object>> list = stringRedisTemplate.opsForStream().read(
        Consumer.from("g1", "c1"),              // 消费者组 g1，消费者 c1
        StreamReadOptions.empty()
                .count(1)                        // 一次取一条
                .block(Duration.ofSeconds(2)),   // 队列为空时阻塞 2 秒，不是忙轮询
        StreamOffset.create(queueName, ReadOffset.lastConsumed())  // 从未消费的消息开始读
);
```

几个参数解释：

- **Consumer.from("g1", "c1")**：消费者组叫 `g1`，组内的消费者叫 `c1`。如果部署多台机器，每台起一个不同的消费者名（c1、c2、c3），同一条消息只会被一台机器处理，天然负载均衡。
- **count(1)**：每次读一条，处理完再读下一条。如果业务快也可以加大。
- **block(Duration.ofSeconds(2))**：阻塞等待 2 秒。如果队列为空，不是立刻返回空然后疯狂循环（那样会打满 CPU），而是挂起等待，最多等 2 秒。这 2 秒内如果有新消息进来立刻返回，没有就超时返回空，进入下一轮循环。
- **ReadOffset.lastConsumed()**：只读没被消费过的消息。已确认的消息不会再读。

### 5.3 读到消息后做什么

```java
MapRecord<String, Object, Object> mapRecord = list.get(0);
Map<Object, Object> map = mapRecord.getValue();

// BeanUtil.fillBeanWithMap 把 Map 自动映射成 VoucherOrder 对象
VoucherOrder voucherOrder = BeanUtil.fillBeanWithMap(
        map, new VoucherOrder(), true);

// 交给实际处理方法
handlerVoucherOrder(voucherOrder);

// 处理成功 → ACK 确认
stringRedisTemplate.opsForStream().acknowledge(
        queueName, "g1", mapRecord.getId());
```

`acknowledge` 就是告诉 Redis："这条消息我处理完了，你可以从 Pending 列表里删掉它。"

### 5.4 消息处理失败怎么办——Pending 兜底

`handlerVoucherOrder` 里如果抛了异常，消息不会 ACK，会一直留在 Pending 列表。所以 `catch` 块里调了 `handlePendingOrder()`：

```java
private void handlePendingOrder() {
    while (true) {
        List<MapRecord<String, Object, Object>> list = stringRedisTemplate.opsForStream().read(
                Consumer.from("g1", "c1"),
                StreamReadOptions.empty().count(1),
                StreamOffset.create(queueName, ReadOffset.from("0"))  // "0"=从 Pending 列表开头读
        );
        if (list == null || list.isEmpty()) {
            break;  // Pending 清空，退出
        }
        // 同样处理 → 同样 ACK
    }
}
```

这里 `ReadOffset.from("0")` 就是关键区别：它不从新消息开始读，而是从 **Pending 列表的最早一条** 开始读。这样之前处理失败的消息会被重新捞出来再试一次。

> **Pending 机制的意义**：如果消费者在写数据库的时候挂了（进程崩溃、机器断电），消息没来得及 ACK，等服务重启后 Pending 处理会把这条消息捞回来重新消费。这就是消息队列的"至少一次"投递保证。

### 5.5 handlerVoucherOrder 内部为什么还要加锁

```java
private void handlerVoucherOrder(VoucherOrder voucherOrder) {
    Long userID = voucherOrder.getUserId();
    RLock lock = redissonClient.getLock("lock:order:" + userID);
    boolean locked = lock.tryLock();
    if (!locked) {
        return;  // 同一用户的消息正在被另一个消费者处理，跳过
    }
    try {
        proxy.createOrder(voucherOrder);
    } finally {
        lock.unlock();
    }
}
```

Lua 脚本已经保证了「一人一单」的资格校验，为什么这里还要加锁？因为 **资格校验和数据库写入是分离的**。极端情况下，同一个用户在短时间内发起了两次请求，两次 Lua 脚本执行时都查不到已下单记录（都还没来得及写数据库），两条消息都会入队。后台消费时，第一条写库成功，第二条如果不加锁直接写库，就会产生重复订单。

所以这里用 Redisson 分布式锁兜底，同一用户 ID 的消息串行处理，第二条进来时发现第一条还没处理完（锁没释放），直接跳过。`createOrder` 内部还有一次数据库查询做最终判断，三重保障。

---

## 6. 整体架构回顾

把整个流程串起来看一遍：

```
用户点击秒杀
    │
    ▼
Controller → Service.seckillVoucher()
    │
    ├── 预生成订单ID（RedisIdWorker）
    │
    ├── 执行 seckill.lua（Redis 服务端原子执行）
    │       ├── 库存检查
    │       ├── 重复下单检查
    │       ├── 扣库存 + 标记用户
    │       └── XADD → stream.orders
    │
    └── 立刻返回 Result.ok(orderID) 给用户
            │
            │  【异步，用户无感】
            ▼
  后台线程 VocherOrderHandler
            │
            ├── XREADGROUP 拉取消息（阻塞2秒）
            │
            ├── 解析消息 → VoucherOrder 对象
            │
            ├── handlerVoucherOrder()
            │       ├── Redisson 分布式锁（用户级）
            │       └── proxy.createOrder()（事务写库）
            │
            └── XACK 确认消息
```

**这样做的好处**：

- 用户请求响应极快（只跑 Lua 脚本，不碰数据库）
- 数据库写入压力被消息队列削峰填谷
- Lua 原子性保证库存扣减和一人一单判断不会并发出错
- Pending + ACK 机制保证消息不丢
- 消费者组支持多实例部署，横向扩容简单

---

## 7. 小结

从 `synchronized` 到 Redis Stream，秒杀方案经过了两次关键升级：

1. **加锁 → Lua 脚本**：把资格校验从 Java 层搬到 Redis 层，利用 Redis 单线程实现真正的原子操作，避免了「判断和扣减之间有间隙」的并发问题。
2. **同步写库 → 消息队列异步**：用户不用等数据库，接口响应从"数据库事务耗时"变成"一次 Redis 网络往返"，吞吐量提升了一个数量级。

Redis Stream 作为消息队列虽然没有 RabbitMQ 那么功能齐全，但在**已经依赖 Redis 的项目里**是一个非常自然的选择，代码量少、部署零成本，对于异步秒杀这种场景完全够用。

---

## 8. 源码附上

### 8.1 seckill.lua

```lua
--  获取优惠券ID（优惠卷编号）
local voucherId = ARGV[1]
-- 用户ID
local userId = ARGV[2]
-- 订单ID （创建的订单编号）
local orderId = ARGV[3]

-- 操作数据
-- 库存Key
local stockKey = 'seckill:stock:' .. voucherId
-- 订单Key
local orderKey = 'seckill:order:' .. voucherId

-- 脚本业务
-- 判断库存是否充足，不足则返回失败
local stock = tonumber(redis.call('GET', stockKey))
if stock == nil or stock <= 0 then
    return 1
end
-- 库存充足，判断用户是否已经下单（限制一人一单）
if(redis.call('sismember', orderKey, userId) == 1) then
--     标识用户已经下过单
    return 2
end

-- 用户没有下过单，且库存充足，需要减库存，添加订单
redis.call('incrby', stockKey, -1)
redis.call('sadd', orderKey, userId)

--  新增消息Redis Stream 队列模式，将任务放入队列中
--  消息内容：用户ID、订单ID、优惠券ID
redis.call('XADD', 'stream.orders','*','userId',userId,'id',orderId,'voucherId',voucherId)
return 0
```

### 8.2 IVoucherOrderService.java

```java
package com.hmdp.service;

import com.hmdp.dto.Result;
import com.hmdp.entity.VoucherOrder;
import com.baomidou.mybatisplus.extension.service.IService;

public interface IVoucherOrderService extends IService<VoucherOrder> {

    Result seckillVoucher(Long voucherId);

    void createOrder(VoucherOrder voucherOrder);
}
```

### 8.3 VoucherOrderController.java

```java
package com.hmdp.controller;

import com.hmdp.dto.Result;
import com.hmdp.service.IVoucherOrderService;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

import javax.annotation.Resource;

@RestController
@RequestMapping("/voucher-order")
public class VoucherOrderController {

    @Resource
    private IVoucherOrderService voucherOrderService;

    @PostMapping("seckill/{id}")
    public Result seckillVoucher(@PathVariable("id") Long voucherId) {
        return voucherOrderService.seckillVoucher(voucherId);
    }
}
```

### 8.4 VoucherOrderServiceImpl.java（完整版，含 Stream 消费者）

```java
package com.hmdp.service.impl;

import cn.hutool.core.bean.BeanUtil;
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
import org.springframework.data.redis.connection.stream.*;
import org.springframework.data.redis.core.StringRedisTemplate;
import org.springframework.data.redis.core.script.DefaultRedisScript;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import javax.annotation.PostConstruct;
import javax.annotation.Resource;
import java.time.Duration;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

@Slf4j
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

    @Resource
    private RedissonClient redissonClient;

    private static final DefaultRedisScript<Long> SECKILL_SCRIPT;
    static {
        SECKILL_SCRIPT = new DefaultRedisScript<>();
        SECKILL_SCRIPT.setLocation(new ClassPathResource("seckill.lua"));
        SECKILL_SCRIPT.setResultType(Long.class);
    }

    private static final ExecutorService SECKILL_EXECUTOR_SERVICE =
            Executors.newFixedThreadPool(10);

    @PostConstruct
    private void init() {
        SECKILL_EXECUTOR_SERVICE.submit(new VocherOrderHandler());
    }

    private class VocherOrderHandler implements Runnable {
        String queueName = "stream.orders";

        @Override
        public void run() {
            while (true) {
                try {
                    // 1. 从 Redis Stream 队列中取出订单消息
                    List<MapRecord<String, Object, Object>> list = stringRedisTemplate.opsForStream().read(
                            Consumer.from("g1", "c1"),
                            StreamReadOptions.empty().count(1).block(Duration.ofSeconds(2)),
                            StreamOffset.create(queueName, ReadOffset.lastConsumed())
                    );
                    // 2. 判断消息是否获取成功
                    if (list == null || list.isEmpty()) {
                        continue;
                    }
                    // 3. 解析消息内容
                    MapRecord<String, Object, Object> mapRecord = list.get(0);
                    Map<Object, Object> map = mapRecord.getValue();
                    VoucherOrder voucherOrder = BeanUtil.fillBeanWithMap(
                            map, new VoucherOrder(), true);

                    // 4. 处理订单
                    handlerVoucherOrder(voucherOrder);

                    // 5. ACK 确认，Redis 从 Pending 中移除该消息
                    stringRedisTemplate.opsForStream().acknowledge(
                            queueName, "g1", mapRecord.getId());
                } catch (Exception e) {
                    log.error("用户下单失败，创建订单异常", e);
                    handlePendingOrder();
                }
            }
        }

        private void handlePendingOrder() {
            while (true) {
                try {
                    // 从 Pending 列表中取出未确认的消息（"0" 表示从 Pending 开头读取）
                    List<MapRecord<String, Object, Object>> list = stringRedisTemplate.opsForStream().read(
                            Consumer.from("g1", "c1"),
                            StreamReadOptions.empty().count(1),
                            StreamOffset.create(queueName, ReadOffset.from("0"))
                    );
                    if (list == null || list.isEmpty()) {
                        break;
                    }
                    MapRecord<String, Object, Object> mapRecord = list.get(0);
                    Map<Object, Object> map = mapRecord.getValue();
                    VoucherOrder voucherOrder = BeanUtil.fillBeanWithMap(
                            map, new VoucherOrder(), true);

                    handlerVoucherOrder(voucherOrder);

                    stringRedisTemplate.opsForStream().acknowledge(
                            queueName, "g1", mapRecord.getId());
                } catch (Exception e) {
                    log.error("用户下单失败，创建订单异常", e);
                }
            }
        }
    }

    private IVoucherOrderService proxy;

    private void handlerVoucherOrder(VoucherOrder voucherOrder) {
        Long userID = voucherOrder.getUserId();
        RLock lock = redissonClient.getLock("lock:order:" + userID);
        boolean locked = lock.tryLock();
        if (!locked) {
            log.error("用户{" + userID + "}下单失败，获取锁失败");
            return;
        }
        try {
            proxy.createOrder(voucherOrder);
        } catch (Throwable e) {
            log.error("用户{" + userID + "}下单失败，创建订单失败", e);
        } finally {
            lock.unlock();
        }
    }

    @Override
    public Result seckillVoucher(Long voucherId) {
        Long userID = UserHolder.getUser().getId();
        Long orderID = redisIdWorker.nextId("order");

        // 执行 Lua 脚本：0=有资格 1=库存不足 2=已下单
        Long result = stringRedisTemplate.execute(
                SECKILL_SCRIPT,
                Collections.emptyList(),
                voucherId.toString(),
                userID.toString(),
                String.valueOf(orderID));

        int r = result.intValue();
        if (r == 1) {
            return Result.fail("库存不足");
        }
        if (r == 2) {
            return Result.fail("您已下单过");
        }

        proxy = (IVoucherOrderService) AopContext.currentProxy();
        return Result.ok(orderID);
    }

    @Transactional
    public void createOrder(VoucherOrder voucherOrder) {
        Long userID = voucherOrder.getUserId();
        Long voucherId = voucherOrder.getVoucherId();

        // 二次检查：防止极端情况下的重复订单
        int count = query().eq("user_id", userID).eq("voucher_id", voucherId).count();
        if (count > 0) {
            log.error("用户{" + userID + "}已购买过该优惠卷");
            return;
        }

        // 扣减库存（乐观锁）
        boolean result = seckillVoucherService.update()
                .setSql("stock = stock - 1")
                .eq("voucher_id", voucherId)
                .gt("stock", 0)
                .update();
        if (!result) {
            log.error("用户{" + userID + "}下单失败，优惠卷已售罄");
            return;
        }

        // 保存订单
        save(voucherOrder);
    }
}
```

---

> **提示**：运行此代码前，请确保：
>
> - Redis 5.0 或以上版本（Stream 是 5.0 引入的）
> - 启动类上配置了 `@EnableAspectJAutoProxy(exposeProxy = true)`
> - 已在 Redis 中手动创建消费者组：`XGROUP CREATE stream.orders g1 0 MKSTREAM`（或代码中自动创建）
> - `seckill.lua` 放在 `src/main/resources` 目录下

---

希望这篇博客能帮你理清「Redis Stream 异步秒杀」的整个链路。从同步阻塞到异步削峰，从 Java 锁到 Lua 原子操作，每一步都是面对真实并发场景时不得不做的取舍。如有疑问，欢迎交流讨论！
*（内容由AI生成，仅供参考）*
