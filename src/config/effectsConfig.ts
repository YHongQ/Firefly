import type { SakuraConfig } from "../types/config";

// 特效配置 - 集中管理所有动画特效

export const sakuraConfig: SakuraConfig = {
	// 是否启用雪花特效
	enable: true,

	// 是否允许用户在设置中切换
	switchable: true,

	// 雪花数量
	sakuraNum: 25,

	// 雪花越界限制次数，-1为无限循环
	limitTimes: -1,

	// 雪花尺寸
	size: {
		// 雪花最小尺寸倍数
		min: 0.4,
		// 雪花最大尺寸倍数
		max: 1.0,
	},

	// 雪花不透明度
	opacity: {
		// 雪花最小不透明度
		min: 0.4,
		// 雪花最大不透明度
		max: 0.95,
	},

	// 雪花移动速度
	speed: {
		// 水平移动
		horizontal: {
			// 水平移动速度最小值
			min: -0.8,
			// 水平移动速度最大值
			max: 0.8,
		},
		// 垂直移动
		vertical: {
			// 垂直移动速度最小值
			min: 1.0,
			// 垂直移动速度最大值
			max: 2.5,
		},
		// 旋转速度
		rotation: 0.02,
		// 消失速度，不应大于最小不透明度
		fadeSpeed: 0.02,
	},

	// 层级，确保雪花在合适的层级显示
	zIndex: 100,
};