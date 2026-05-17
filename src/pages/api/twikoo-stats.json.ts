import { getSortedPosts } from "@/utils/content-utils";
import { commentConfig } from "@/config";

interface TwikooCounterResponse {
    code: number;
    data?: {
        url: string;
        title?: string;
        time: number;
    } | {
        url: string;
        time: number;
    } | Array<{
        url: string;
        title?: string;
        time: number;
    }>;
    errorMessage?: string;
}

// 创建带超时的fetch包装器
async function fetchWithTimeout(url: string, options: RequestInit, timeout = 5000): Promise<Response> {
    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), timeout);
    
    try {
        const response = await fetch(url, {
            ...options,
            signal: controller.signal
        });
        clearTimeout(timeoutId);
        return response;
    } catch (error) {
        clearTimeout(timeoutId);
        throw error;
    }
}

export async function GET() {
    // 检查是否配置了Twikoo
    if (commentConfig.type !== 'twikoo' || !commentConfig.twikoo?.envId) {
        console.log('Twikoo未配置');
        return new Response(
            JSON.stringify({ 
                totalViews: 0,
                totalArticles: 0,
                articles: [],
            }),
            { status: 200, headers: { 'Content-Type': 'application/json' } }
        );
    }

    try {
        // 获取所有文章
        const posts = await getSortedPosts();
        
        // 用于汇总统计数据
        let totalViews = 0;
        
        // 查询每篇文章的访问量
        const articles = [];
        
        // 限制并发请求数量，避免过多请求导致服务器压力
        const MAX_CONCURRENT_REQUESTS = 3;
        const queue = [...posts];
        
        while (queue.length > 0) {
            const batch = queue.splice(0, MAX_CONCURRENT_REQUESTS);
            
            const batchResults = await Promise.allSettled(batch.map(async (post) => {
                const articleUrl = `/posts/${post.id}`;
                
                // 查询单篇文章的统计数据
                const requestBody = {
                    event: 'COUNTER_GET',
                    url: articleUrl,
                    path: articleUrl,
                    accessToken: commentConfig.twikoo?.accessToken || '',
                    region: commentConfig.twikoo?.region || '',
                    lang: commentConfig.twikoo?.lang || 'zh-CN',
                };

                try {
                    const response = await fetchWithTimeout(commentConfig.twikoo!.envId, {
                        method: 'POST',
                        headers: {
                            'Content-Type': 'application/json',
                        },
                        body: JSON.stringify(requestBody),
                    }, 8000); // 8秒超时

                    if (!response.ok) {
                        console.error(`获取文章${articleUrl}的访问量HTTP错误:`, response.status);
                        return {
                            id: post.id,
                            title: post.data.title,
                            views: 0,
                            url: articleUrl,
                        };
                    }

                    const result: TwikooCounterResponse = await response.json();
                    
                    if (result.code === 0) {
                        let articleViews = 0;
                        if (result.data) {
                            if (Array.isArray(result.data)) {
                                // 如果返回数组，取第一个匹配的
                                const matchedData = result.data.find(item => item.url === articleUrl);
                                articleViews = matchedData?.time || 0;
                            } else if (typeof result.data === 'object') {
                                // 如果返回对象，直接取time值
                                articleViews = result.data.time || 0;
                            }
                        }
                        
                        return {
                            id: post.id,
                            title: post.data.title,
                            views: articleViews,
                            url: articleUrl,
                        };
                    } else {
                        console.warn(`获取文章${articleUrl}的访问量失败，错误码: ${result.code}`, result.errorMessage);
                        return {
                            id: post.id,
                            title: post.data.title,
                            views: 0,
                            url: articleUrl,
                        };
                    }
                } catch (articleError) {
                    console.error(`获取文章${articleUrl}的访问量网络错误:`, articleError);
                    return {
                        id: post.id,
                        title: post.data.title,
                        views: 0,
                        url: articleUrl,
                    };
                }
            }));
            
            // 处理批次结果
            for (const result of batchResults) {
                if (result.status === 'fulfilled') {
                    const article = result.value;
                    totalViews += article.views;
                    articles.push(article);
                } else {
                    // 如果某个请求出错，添加默认值
                    console.error('请求失败:', result.reason);
                }
            }
        }
        
        // 返回汇总统计数据
        const stats = {
            totalViews,
            totalArticles: posts.length,
            articles,
        };

        return new Response(JSON.stringify(stats), {
            status: 200,
            headers: { 'Content-Type': 'application/json' },
        });
    } catch (error) {
        console.error('获取Twikoo统计数据失败:', error);
        return new Response(
            JSON.stringify({ 
                totalViews: 0,
                totalArticles: 0,
                articles: [],
            }),
            { status: 200, headers: { 'Content-Type': 'application/json' } }
        );
    }
}