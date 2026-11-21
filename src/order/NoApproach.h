/* 注入探测到不能攻击敌人的单位时，拦截原版的靠近动作。 */
#pragma once

extern "C" __declspec(dllexport) unsigned __cdecl NoApproach_Hook(void* regs);
