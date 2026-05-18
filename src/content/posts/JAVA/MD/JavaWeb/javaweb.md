---
title: JAVA WEB 基础
published: 2026-04-10
description: JAVA WEB 基础知识，参考黑马程序员
tags: [JAVA, WEB, 三层架构, 分层解耦, IOC, DI, 数据库连接池, lombok, SQL预编译, Mybatis 版本差异, JWT令牌技术, 校验拦截框架逻辑, 事务管理, AOP 面向切面编程]
category: 学习记录
# image: image/JAVA/1733811477039.png
draft: false
---



## JAVA WEB

### 1. 三层架构
![alt text](image.png)
![alt text](image-1.png)

### 2. 分层解耦
![alt text](image-2.png)
![alt text](image-3.png)

1. IOC(控制反转)
![alt text](image-5.png)
![alt text](image-6.png)

2. DI(依赖注入)
![alt text](image-9.png)

![alt text](image-10.png)


### 3. 数据库连接池

![alt text](image-11.png)


### 4. lombok

![alt text](image-12.png)

### 5. SQL预编译

![alt text](image-13.png)


### 6. Mybatis 版本差异
![alt text](image-14.png)


### 7. Restful 分格推荐
![alt text](image-15.png)
![alt text](image-16.png)


### 8. JWT令牌技术
![alt text](image-17.png)
![alt text](image-18.png)

统一校验
1. Filter (Java Web组件之一，用于在请求处理过程中拦截请求，进行校验)

![alt text](image-19.png)
![alt text](image-20.png)

2. Interceptor (Spring框架组件之一，用于在请求处理过程中拦截请求，进行校验)


![alt text](image-21.png)

![alt text](image-22.png)

校验拦截框架逻辑
![alt text](image-23.png)



- 全局异常处理器
@RestControllerAdvice (用于定义全局异常处理方法)
@ExceptionHandler (用于指定处理的异常类型)


### 9. 事务管理

@Transactional (用于开启事务)

![alt text](image-24.png)

事务回滚设置异常类型范围，用于指定在哪些异常情况下回滚事务。
``` java

@Transactional(rollbackFor = Exception.class)

```

![alt text](image-25.png)

![alt text](image-26.png)

 事务传播行为：required (默认值，嵌套进入事务时，继续使用当前事务；如果当前事务不存在，创建新的事务) requiresNew (事务嵌套时，创建新的事务)
![alt text](image-27.png)


### 10. AOP 面向切面编程

![alt text](image-28.png)
![alt text](image-30.png)
AOP的实现是通过动态代理技术实现的，具体实现原理如下：
1. 代理对象创建：当需要对某个类的方法进行增强时，AOP框架会创建一个代理对象，这个代理对象实现了与目标对象相同的接口或者继承了目标对象的类。
2. 方法拦截：当调用代理对象的方法时，AOP框架会拦截这个方法调用，并根据配置的切面逻辑执行相应的增强操作。
![alt text](image-31.png)

![alt text](image-32.png)

@Pointcut (用于定义切面的切点表达式) 可以反复使用


AOP 多切面执行顺序：
 1. 与类名的顺序决定切片执行顺序
 2. 也可以通过 @Order 注解来指定切面的执行顺序，默认值为 0，值越小优先级越高

切入点表达式：
1. execution(* com.example.service.*.*(..)) (匹配com.example.service包下的所有方法)
   - ![alt text](image-33.png)
   - ![alt text](image-34.png)
   - 
2. annotation(com.example.annotation.MyAnnotation) (匹配所有使用MyAnnotation注解的方法)

   - 



### 11. 配置

![alt text](image-35.png)

### 12. Bean对象的管理

1. Bean对象的获取

bean对象获取通过IOC容器ApplicationContext实现
 - ApplicationContext (Spring框架的核心接口，用于管理Bean对象)
 - ApplicationContext.getBean(String beanName) (根据Bean的名称获取Bean对象)
 - ApplicationContext.getBean(Class<T> beanType) (根据Bean的类型获取Bean对象)
 - ApplicationContext.getBean(String beanName, Class<T> beanType) (根据Bean的名称和类型获取Bean对象)


2. Bean对象的作用域

![alt text](image-36.png)

通过注解 @Scope 来指定Bean的作用域，默认值为 singleton。

通过@Lazy 注解来指定Bean的懒加载.


3. 第三方Bean对象的管理
![alt text](image-37.png)


### 13. springboot 原理


1. springboot 启动原理

通过依赖注入，将Bean对象注入到需要使用的地方。

2. springboot 自动配置原理

通过 @EnableAutoConfiguration 注解来启用自动配置功能，Spring Boot会根据类路径中的依赖和配置文件中的设置，自动配置应用程序所需的Bean对象和配置。

@ComponentScan (用于指定自动扫描的包路径)
默认值为当前类所在包，也可以通过 @ComponentScan 注解来指定自动扫描的包路径。

Import (用于导入其他配置类)
默认值为空，也可以通过 @Import 注解来导入其他配置类。
![alt text](image-38.png)


![alt text](image-39.png)


![alt text](image-40.png)


![alt text](image-41.png)