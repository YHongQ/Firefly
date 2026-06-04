-- 获取锁中key的值(Redis中存储的锁（键值对）)
local key = KEYS[1]
-- 当前线程标识
local threadID = ARGV[1]

local releaseTime = ARGV[2]

-- 判断是否存在
if redis.call("EXISTS", key) == 0 then
    -- 锁不存在，获取锁
    redis.call("HSET", key, threadID, 1)
    redis.call("EXPIRE", key, releaseTime) -- 锁锁过期时间
    return 1
end

-- 锁存在，判断是否是当前线程获取锁
if redis.call("HEXISTS", key, threadID) == 1 then
    -- 是当前线程获取锁，重入锁
    redis.call("HINCRBY", key, threadID, 1)
    redis.call("EXPIRE", key, releaseTime) -- 重置锁锁过期时间
    return 1
end
return 0  -- 锁存在且不是当前线程获取锁，获取锁失败
