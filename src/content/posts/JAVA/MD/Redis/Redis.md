---
title: Redis黑马课程学习
published: 2026-05-31
tags: [Redis, 黑马课程,学习笔记,JAVA]
pinned: true
category: 学习笔记
draft: false
---
# Redis黑马课程学习

本课程介绍学习Redis的基本概念、数据结构、操作命令、集群部署等。

## 1. Redis介绍

Redis是一个基于内存的键值存储数据库，用于存储和检索数据。它支持多种数据结构，如字符串、列表、哈希、集合等。

- 支持数据持久化。

**NoSQL数据库** ：是一种非关系型数据库，它不使用关系型数据库的表和行结构，而是使用键值对存储数据。
![1779432758589](image/Redis/1779432758589.png)

## 2. Redis 常用命令

### 2.1 Redis 数据结构

1. Redis是键值对存储数据库，每个键对应一个值。
2. Redis支持多种数据结构，如字符串、列表、哈希、集合等。
3. 不同的数据结构有不同的操作命令。
   ![1779518651818](image/Redis/1779518651818.png)

### 2.2 Redis 常用命令

1. KEYS  命令：用于查看键名。

   - 语法：KEY [pattern]
   - 示例：KEY * # 查看所有键名
   - 示例：KEY *key* # 查看包含"key"的键名
2. help [command] 命令：用于查看指定命令的详细信息。
3. del 命令：用于删除指定键。

   - 语法：del [key]
   - 示例：del key1 key2 # 删除key1和key2
4. exist 命令：用于检查键是否存在。

   - 语法：exists [key]
   - 返回值：1表示存在，0表示不存在
5. expire 命令：用于设置键的过期时间,有效期到期后键将自动删除。

   - 语法：expire [key] [seconds]
   - 示例：expire key1 60 # 设置key1的过期时间为60秒
   - 返回值：1表示设置成功，0表示设置失败
6. ttl 命令：用于查看键的过期时间。

   - 语法：ttl [key]
   - 示例：ttl key1 # 查看key1的过期时间
   - 返回值：-1表示永不过期，秒为单位

### 2.3 String 类型（Value 为字符串）命令

1. SET 命令：用于设置键值对。

   - 语法：SET [key] [value]
   - 示例：SET key1 value1 # 设置key1的值为value1
   - 返回值：OK表示设置成功
2. GET 命令：用于获取键值对。

   - 语法：GET [key]
   - 示例：GET key1 # 获取key1的值
   - 返回值：value1
3. MSET 命令：用于设置多个键值对。

   - 语法：MSET [key] [value] ...
   - 示例：MSET key1 value1 key2 value2 # 设置key1和key2的值
   - 返回值：OK表示设置成功
4. MGET 命令：用于获取多个键值对。

   - 语法：MGET [key] ...
   - 示例：MGET key1 key2 # 获取key1和key2的值
   - 返回值：value1 value2
5. INCR 命令：用于将键的值增加1。（仅对数值型键值对有效，其他类型键会报错。）

   - 语法：INCR [key]
   - 示例：INCR key1 # 将key1的值增加1
6. INCRBY 命令：用于将键的值增加指定的增量。

   - 语法：INCRBY [key] [increment]
   - 示例：INCRBY key1 5 # 将key1的值增加5
7. DECR 命令：用于将键的值减少1。（仅对数值型键值对有效，其他类型键会报错。）

   - 语法：DECR [key]
   - 示例：DECR key1 # 将key1的值减少1
8. INCRBYFLOAT 命令：用于将键的值增加指定的浮点数增量。

   - 语法：INCRBYFLOAT [key] [increment]
   - 示例：INCRBYFLOAT key1 5.0 # 将key1的值增加5.0
9. SETNX 命令：用于设置键值对，仅当键不存在时才设置。

   - 语法：SETNX [key] [value]
   - 示例：SETNX key1 value1 # 如果key1不存在，设置key1的值为value1
   - 返回值：1表示设置成功，0表示键已存在
10. SETEX 命令：用于设置键值对，并设置过期时间。

    - 语法：SEEX [key] [seconds] [value]
    - 示例：SEEX key1 60 value1 # 设置key1的值为value1，过期时间为60秒
    - 返回值：OK表示设置成功

---

> Redis没有表结构，不存在唯一索引，因此在键值对上对键进行**层级结构命名**，多个单词间用":"分隔。
>
> 示例：user:123:info
>
> 项目：业务名：类型：键值

---

### 2.4 Hash 类型（Value 为哈希表）命令

- Hash 类型，Value是一个无序字典集合，类似Java中的HashMap。
- 因为值为哈希表，所以Hash类型支持键值对的存储和检索。

1. HSET 命令：用于设置哈希表的键值对。

   - 语法：HSET [key] [field] [value]
   - 示例：HSET key1 field1 value1 # 设置key1的哈希表字段field1的值为value1
   - 返回值：OK表示设置成功
2. HGET 命令：用于获取哈希表的键值对。

   - 语法：HGET [key] [field]
   - 示例：HGET key1 field1 # 获取key1的哈希表字段field1的值
   - 返回值：value1
3. HGETALL 命令：用于获取哈希表的所有键值对。

   - 语法：HGETALL [key]
   - 示例：HGETALL key1 # 获取key1的所有哈希表键值对
   - 返回值：field1:value1 field2:value2 ...
4. HDEL 命令：用于删除哈希表的键值对。

   - 语法：HDEL [key] [field] ...
   - 示例：HDEL key1 field1 field2 # 删除key1的哈希表字段field1和field2
   - 返回值：OK表示删除成功
5. HMSET 命令：用于设置多个哈希表的键值对。

   - 语法：HMSET [key] [field] [value] ...
   - 示例：HMSET key1 field1 value1 field2 value2 # 设置key1的哈希表字段field1和field2的值
   - 返回值：OK表示设置成功
6. HMGET 命令：用于获取多个哈希表的键值对。

   - 语法：HMGET [key] [field] ...
   - 示例：HMGET key1 field1 field2 # 获取key1的哈希表字段field1和field2的值
   - 返回值：value1 value2
7. HKEYS 命令：用于获取哈希表的所有键。

   - 语法：HKEYS [key]
   - 示例：HKEYS key1 # 获取key1的所有哈希表字段
   - 返回值：field1 field2 ...
8. HVALS 命令：用于获取哈希表的所有值。

   - 语法：HVALS [key]
   - 示例：HVALS key1 # 获取key1的所有哈希表字段值
   - 返回值：value1 value2 ...
9. HINCRBY 命令：用于将哈希表的键值对增加指定的增量。

   - 语法：HINCRBY [key] [field] [increment]
   - 示例：HINCRBY key1 field1 5 # 将key1的哈希表字段field1的值增加5
   - 返回值：value1
10. HSETNX 命令：用于设置哈希表的键值对，仅当键不存在时才设置。

    - 语法：HSETNX [key] [field] [value]
    - 示例：HSETNX key1 field1 value1 # 如果key1的哈希表字段field1不存在，设置key1的哈希表字段field1的值为value1
    - 返回值：1表示设置成功，0表示键已存在

### 2.5 List 类型（Value 为列表）命令

- List 类型，Value是一个有序列表，类似Java中的LinkedList,可视为双向链表。
- 特点是：有序、元素可重复、插入和删除快、查询效率低。

1. LPUSH 命令：用于将元素插入到列表的左端。

   - 语法：LPUSH [key] [value]
   - 示例：LPUSH key1 value1 # 将value1插入到列表key1的左端
   - 返回值：列表的长度
2. RPUSH 命令：用于将元素插入到列表的右端。

   - 语法：RPUSH [key] [value]
   - 示例：RPUSH key1 value1 # 将value1插入到列表key1的右端
   - 返回值：列表的长度
3. LPOP 命令：用于从列表的左端弹出元素。**如果列表为空，返回NULL**。

   - 语法：LPOP [key]
   - 示例：LPOP key1 # 从列表key1的左端弹出元素
   - 返回值：value1
4. RPOP 命令：用于从列表的右端弹出元素。**如果列表为空，返回NULL**

   - 语法：RPOP [key]
   - 示例：RPOP key1 # 从列表key1的右端弹出元素
   - 返回值：value1
5. LRANGE 命令：用于获取列表的指定范围内的元素。

   - 语法：LRANGE [key] [start] [end]
   - 示例：LRANGE key1 0 1 # 获取列表key1的第0个元素到第1个元素
   - 返回值：value1 value2
6. LLEN 命令：用于获取列表的长度。

   - 语法：LLEN [key]
   - 示例：LLEN key1 # 获取列表key1的长度
   - 返回值：2
7. BLPOP 命令：用于从列表的左端弹出元素，**若列表为空则阻塞等待**。

   - 语法：BLPOP [key] [timeout]
   - 示例：BLPOP key1 60 # 从列表key1的左端弹出元素，若列表为空则阻塞等待60秒
   - 返回值：value1
   - 说明：若列表key1为空，BLPOP命令会阻塞等待60秒，若60秒内有元素插入到列表key1，BLPOP命令会立即返回该元素。
   - 若60秒内没有元素插入到列表key1，BLPOP命令会返回NULL。
8. BRPOP 命令：用于从列表的右端弹出元素，**若列表为空则阻塞等待**。

---

1. 如何利用List结构实现队列
   - 入口和出口在不同的侧
2. 如何利用List结构实现栈
   - 入口和出口都在列表的同侧
3. 如何利用List结构实现阻塞队列
   - 入口和出口在不同的侧
   - 出队是BLPOP命令，入队是LPUSH命令，通过等待BLPOP命令的超时时间，实现阻塞队列。

### 2.6 Set 类型（Value 为集合）命令

- Set 类型，Value是一个无序集合，类似Java中的HashSet,可视为哈希表的特殊实现。
- 特点是：无序、元素不可重复、插入和删除快、查询效率高。

1. SADD 命令：用于将元素插入到集合中。

   - 语法：SADD [key] [value]
   - 示例：SADD key1 value1 # 将value1插入到集合key1中
   - 返回值：OK表示插入成功
2. SREM 命令：用于从集合中删除元素。

   - 语法：SREM [key] [value]
   - 示例：SREM key1 value1 # 从集合key1中删除元素value1
   - 返回值：OK表示删除成功
   - 说明：若集合key1中不存在元素value1，SREM命令会返回OK。
3. SCARD 命令：用于获取集合的元素数量。

   - 语法：SCARD [key]
   - 示例：SCARD key1 # 获取集合key1的元素数量
4. SISMEMBER 命令：用于判断元素是否在集合中。

   - 语法：SISMEMBER [key] [value]
   - 示例：SISMEMBER key1 value1 # 判断集合key1中是否存在元素value1
   - 返回值：1表示存在，0表示不存在
   - 说明：若集合key1中不存在元素value1，SISMEMBER命令会返回0。
   - 若集合key1中存在元素value1，SISMEMBER命令会返回1。
5. SMEMBERS 命令：用于获取集合中的所有元素。

   - 语法：SMEMBERS [key]
   - 示例：SMEMBERS key1 # 获取集合key1中的所有元素
   - 返回值：value1 value2 ...
6. SINTER 命令：用于获取多个集合的交集。

   - 语法：SINTER [key1] [key2] ...
   - 示例：SINTER key1 key2 # 获取集合key1和集合key2的交集
   - 返回值：value1 value2 ...
7. SUNION 命令：用于获取多个集合的并集。

   - 语法：SUNION [key1] [key2] ...
   - 示例：SUNION key1 key2 # 获取集合key1和集合key2的并集
   - 返回值：value1 value2 ...
8. SDIFF 命令：用于获取多个集合的差集。

   - 语法：SDIFF [key1] [key2] ...
   - 示例：SDIFF key1 key2 # 获取集合key1和集合key2的差集
   - 返回值：value1 value2 ...

### 2.7 SortedSet 类型（Value 为有序集合）命令

- SortedSet 类型，Value是一个有序集合，类似Java中的TreeSet功能，但是redis中底层是一个跳表（SkipList）加hash表。通过每个元素带一个score属性，对元素根据score排序。
- 特点是：可排序，查询效率高。

1. ZADD 命令：用于将元素插入到有序集合中。

   - 语法：ZADD [key] [score] [value]
   - 示例：ZADD key1 100 value1 # 将value1插入到有序集合key1中，score为100
   - 返回值：OK表示插入成功
2. ZREM 命令：用于从有序集合中删除元素。

   - 语法：ZREM [key] [value]
   - 示例：ZREM key1 value1 # 从有序集合key1中删除元素value1
   - 返回值：OK表示删除成功
   - 说明：若有序集合key1中不存在元素value1，ZREM命令会返回OK。
3. ZCARD 命令：用于获取有序集合的元素数量。

   - 语法：ZCARD [key]
   - 示例：ZCARD key1 # 获取有序集合key1的元素数量
   - 返回值：2
   - 说明：有序集合key1中存在2个元素。
4. ZSCORE 命令：用于获取有序集合中元素的score值。

   - 语法：ZSCORE [key] [value]
   - 示例：ZSCORE key1 value1 # 获取有序集合key1中元素value1的score值
   - 返回值：100
   - 说明：有序集合key1中元素value1的score值为100。
   - 若有序集合key1中不存在元素value1，ZSCORE命令会返回NULL。
5. ZRANK 命令：用于获取有序集合中元素的排名。

   - 语法：ZRANK [key] [value]
   - 示例：ZRANK key1 value1 # 获取有序集合key1中元素value1的排名
   - 返回值：0
   - 说明：有序集合key1中元素value1的排名为0，从0开始计数。
   - 若有序集合key1中不存在元素value1，ZRANK命令会返回-1。
6. ZCOUNT 命令：用于获取有序集合中范围元素的数量。

   - 语法：ZCOUNT [key] [min] [max]
   - 示例：ZCOUNT key1 0 100 # 获取有序集合key1中score在0到100之间的元素数量
   - 返回值：2
   - 说明：有序集合key1中score在0到100之间的元素数量为2。
7. ZINCRBY 命令：用于对有序集合中元素的score值进行增量操作。

   - 语法：ZINCRBY [key] [increment] [value]
   - 示例：ZINCRBY key1 10 value1 # 对有序集合key1中元素value1的score值进行10的增量操作
   - 返回值：110
   - 说明：有序集合key1中元素value1的score值为110。

---

> 默认排序是按score值升序排序。

## 3. Redis 的JAVA客户端

1. Jedis 是一个基于Java的Redis客户端，用于连接Redis服务器并执行Redis命令。 线程不安全的
2. Lettuce 是一个基于Java的Redis客户端，用于连接Redis服务器并执行Redis命令。 线程安全的。支持Redis的哨兵模式、集群模式、管道模式
3. Redisson 是一个基于Java的Redis客户端，用于连接Redis服务器并执行Redis命令。

> Spring Data Redis 是一个基于Java的Redis客户端，用于连接Redis服务器并执行Redis命令。

### 3.1 Jedis 客户端

1. 引入Maven依赖

```xml
   <dependency>
      <groupId>redis.clients</groupId>
      <artifactId>jedis</artifactId>
      <version>3.7.0</version>
    </dependency>
```

2. 连接Redis服务器

```java

package COM.HONG.test;

import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import redis.clients.jedis.Jedis;

public class JedisConnectTest {
    private Jedis jedis;

    @BeforeEach
    public void setUp() {
        jedis = new Jedis("127.0.0.1", 6379);
        jedis.select(0);
        // 测试是否连接上Redis
        String result = jedis.ping();

        if (result.equals("PONG")) {
            System.out.println("Redis连接成功");
        } else {
            System.out.println("Redis连接失败");
        }
    }

    @Test
    public void testConnect() {
        // 设置数据
        jedis.set("Years", "2023");
        // 获取数据
        String value = jedis.get("Years");
        // 打印获取到的值
        System.out.println("获取到的值: " + value);
    }

    @AfterEach
    public void tearDown() {
        if (jedis != null) {
            jedis.close();
        }
        System.out.println("Redis连接已关闭");
    }
}
```

3. Jedis 连接池

- 连接池可以提高Redis客户端的性能，避免频繁创建和销毁连接。

```java

package COM.HONG.test;

import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;
import redis.clients.jedis.Jedis;
import redis.clients.jedis.JedisPool;
import redis.clients.jedis.JedisPoolConfig;

public class JedisPoolConnectTest {
    // 连接池
    private static JedisPool pool;
    // 连接池配置
    static {
        JedisPoolConfig poolConfig = new JedisPoolConfig();
        poolConfig.setMaxTotal(10); // 最大连接数
        poolConfig.setMaxIdle(5); // 最大空闲连接数

        poolConfig.setMinIdle(0); // 最小空闲连接数

        pool = new JedisPool(poolConfig, "127.0.0.1", 6379);
        Jedis jedis = pool.getResource();

    }

    // 测试连接池
    @Test
    public void testConnect() {
        Jedis jedis = pool.getResource();
        if (jedis != null) {
            System.out.println(jedis.ping());
        }
        String value = jedis.get("Years");
        System.out.println("获取到的值: " + value);
    }

    @AfterEach
    public void tearDown() {
        if (pool != null) {
            pool.close();
        }
        System.out.println("Redis连接池已关闭");
    }
}

```

4. Spring Data Redis

- Spring Data Redis 是一个基于Java的Redis客户端，用于连接Redis服务器并执行Redis命令。它提供了一个高层次的抽象，使得开发者可以更方便地使用Redis。

![1779526576313](image/Redis/1779526576313.png)

* 同样使用Spring-Boot工程，引入Spring Data Redis依赖。

```xml
    <dependency>
        <groupId>org.springframework.boot</groupId>
        <artifactId>spring-boot-starter-data-redis</artifactId>
    </dependency>
```
- 配置Redis连接
```yaml

# 配置Redis连接
spring:
  data:
    redis:
      host: 127.0.0.1
      port: 6379
#      password:
      timeout: 60s
      lettuce:
        pool:
          max-active: 8
          max-idle: 8
          min-idle: 0
```

- 测试代码
```java
package com.hong.redisproj2;


import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.data.redis.connection.RedisConnectionFactory;
import org.springframework.data.redis.core.RedisTemplate;

@SpringBootTest
class Redisproj2ApplicationTests {

    //注入RedisTemplate
    @Autowired
    private RedisTemplate redisTemplate; // 泛型表示支持字符串键和对象值


    @Test
    void contextLoads() {
        // 测试连接是否成功
        redisTemplate.opsForValue().set("Redis1", "Hello Redis!");
        String value = (String) redisTemplate.opsForValue().get("Redis1");
        System.out.println("Redis1: " + value);
    }

}

```


