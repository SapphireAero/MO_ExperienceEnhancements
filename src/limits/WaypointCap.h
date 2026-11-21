/* WaypointCap.h - 路径点上限单机解除 (实现见 WaypointCap.cpp, 单机专用)。
 * .text 直接补丁, 幂等且冲突时自动放弃; 联机绕过 (不装钩, 已装则卸钩)。 */
#pragma once

/* 每循环轮询 (由 SA_EE.cpp 的 PollSelectedFeatures 调用): 懒装 0x6384DC 钩 +
 * 按单机/联机刷新生效上限。 */
void WpCap_Poll(void);
