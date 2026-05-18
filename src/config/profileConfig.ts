import type { ProfileConfig } from "../types/config";

export const profileConfig: ProfileConfig = {
	// 头像
	// 图片路径支持三种格式：
	// 1. public 目录（以 "/" 开头，不优化）："/assets/images/avatar.webp"
	// 2. src 目录（不以 "/" 开头，自动优化但会增加构建时间，推荐）："assets/images/avatar.webp"
	// 3. 远程 URL："https://example.com/avatar.jpg"
	avatar: "assets/images/HONGWallpaper/head.jpg",

	// 名字
	name: "溄",

	// 个人签名
	bio: "Hello, I'm HONG !",

	// 链接配置
	// 已经预装的图标集：fa7-brands，fa7-regular，fa7-solid，material-symbols，simple-icons
	// 访问https://icones.js.org/ 获取图标代码，
	// 如果想使用尚未包含相应的图标集，则需要安装它
	// `pnpm add @iconify-json/<icon-set-name>`
	// showName: true 时显示图标和名称，false 时只显示图标
	links: [
		{
			name: "QQ",
			icon: "fa7-brands:qq",
			// 跳转QQ加好友链接，格式为：https://qm.qq.com/cgi-bin/qm/qr?k=<2142479640>，其中 <your_qq_number> 替换为你的QQ号码
			url: "https://qm.qq.com/q/SqIlKaK9sy",
			showName: false,
		},
		{
			name: "Email",
			icon: "fa7-solid:envelope",
			url: "mailto:hongqiang.yan@qq.com",
			showName: false,
		},
		{
			name: "GitHub",
			icon: "fa7-brands:git", //更换图标为其他版本的 GitHub 图标 
			url: "https://github.com/YHongQ/",
			showName: false,
		},

		// {
		// 	name: "RSS",
		// 	icon: "fa7-solid:rss",
		// 	url: "/rss/",
		// 	showName: false,
		// },
	],
};