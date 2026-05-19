import type { FriendLink, FriendsPageConfig } from "../types/config";

// 可以在src/content/spec/friends.md中编写友链页面下方的自定义内容

// 友链页面配置
export const friendsPageConfig: FriendsPageConfig = {
	// 页面标题，如果留空则使用 i18n 中的翻译
	title: "",

	// 页面描述文本，如果留空则使用 i18n 中的翻译
	description: "",

	// 是否显示底部自定义内容（friends.mdx 中的内容）
	showCustomContent: true,

	// 是否显示评论区，需要先在commentConfig.ts启用评论系统
	showComment: true,

	// 是否开启随机排序配置，如果开启，就会忽略权重，构建时进行一次随机排序
	randomizeSort: false,
};

// 友链配置
export const friendsConfig: FriendLink[] = [
	{
		title: "科比",
		imgurl:
			"https://cn.bing.com/images/search?view=detailV2&ccid=NPA9JbA8&id=A5ECC6D7574A6FBD13075D527AA548565C105F2D&thid=OIP.NPA9JbA80G6ujwa94ZrmmgHaNK&mediaurl=https%3a%2f%2fpic2.zhimg.com%2fv2-e12664feae760d3101ba7d83d47a91b4_r.jpg%3fsource%3d1940ef5c&exph=1920&expw=1080&q=%e7%a7%91%e6%af%94%e5%9b%be%e7%89%87&mode=overlay&FORM=IQFRBA&ck=C7D5429D937A8BA2F09D3BC6E73FFAF4&selectedIndex=0&idpp=serp&ajaxhist=0&ajaxserp=0",
		desc: "科比·布莱恩特（Kobe Bryant），美国职业篮球运动员，被认为是NBA历史上最伟大的球员之一。",
		siteurl: "https://baike.baidu.com/item/%E7%A7%91%E6%AF%94%C2%B7%E5%B8%83%E8%8E%B1%E6%81%A9%E7%89%B9/318773",
		tags: ["Blog"],
		weight: 10, // 权重，数字越大排序越靠前
		enabled: true, // 是否启用
	},

];

// 获取启用的友链并进行排序
export const getEnabledFriends = (): FriendLink[] => {
	const friends = friendsConfig.filter((friend) => friend.enabled);

	if (friendsPageConfig.randomizeSort) {
		return friends.sort(() => Math.random() - 0.5);
	}

	return friends.sort((a, b) => b.weight - a.weight);
};
