/* 只调整本机观战房指针与面板名单。 */
#pragma once

void Observer_Poll(void);

/* 只读查询，独立于 [Features] Observer 开关。 */
int Observer_LocalIsSpectator(void);
