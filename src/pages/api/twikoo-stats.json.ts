import { getSortedPosts } from "@/utils/content-utils";
import { commentConfig } from "@/config";

export async function GET() {
    if (commentConfig.type !== 'twikoo' || !commentConfig.twikoo?.envId) {
        return new Response(JSON.stringify({ 
            success: false,
            error: 'Twikoo未配置',
        }), { status: 200, headers: { 'Content-Type': 'application/json' } });
    }

    try {
        const posts = await getSortedPosts();
        if (posts.length === 0) {
            return new Response(JSON.stringify({
                success: true,
                totalViews: 0,
                articles: [],
            }), { status: 200, headers: { 'Content-Type': 'application/json' } });
        }

        const envId = commentConfig.twikoo.envId.replace(/\/$/, '');
        const accessToken = commentConfig.twikoo.accessToken || '';
        
        const articles = await Promise.all(posts.map(async (post) => {
            const url = `/posts/${post.id}`;
            try {
                const response = await fetch(envId, {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({
                        event: 'COUNTER_GET',
                        url,
                        path: url,
                        accessToken,
                    }),
                });

                const text = await response.text();
                
                if (!response.ok) {
                    console.error(`文章${url} HTTP错误: ${response.status}`);
                    return { id: post.id, views: -1, error: `HTTP ${response.status}` };
                }

                let result;
                try {
                    result = JSON.parse(text);
                } catch {
                    console.error(`文章${url} JSON解析失败: ${text.slice(0, 100)}`);
                    return { id: post.id, views: -1, error: 'JSON解析失败' };
                }

                let views = 0;
                const data = result.data;
                
                if (data !== undefined) {
                    if (typeof data === 'number') {
                        views = data;
                    } else if (Array.isArray(data) && data.length > 0) {
                        const item = data.find((item: any) => item.url === url);
                        views = item ? (item.time || item.value || 0) : 0;
                    } else if (typeof data === 'object') {
                        views = data.time || data.value || data.views || 0;
                    }
                } else if (typeof result === 'object') {
                    views = result.time || result.value || result.views || 0;
                }
                
                return { id: post.id, views };
                
            } catch (error) {
                console.error(`文章${url} 请求失败:`, error);
                return { id: post.id, views: -1, error: String(error) };
            }
        }));

        const totalViews = articles.reduce((sum, art) => sum + (art.views >= 0 ? art.views : 0), 0);
        const hasError = articles.some(art => art.views < 0);
        
        return new Response(JSON.stringify({
            success: !hasError,
            totalViews,
            articles,
            error: hasError ? '部分文章获取失败' : undefined,
        }), { status: 200, headers: { 'Content-Type': 'application/json' } });

    } catch (error) {
        console.error('获取统计数据失败:', error);
        return new Response(JSON.stringify({ 
            success: false,
            error: String(error),
        }), { status: 200, headers: { 'Content-Type': 'application/json' } });
    }
}