/**
* Amtasukaze Compile Target
* Copyright (c) 2017-2018 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#define _USE_MATH_DEFINES
// avisynthにリンクしているので
#define AVS_LINKAGE_DLLIMPORT
#include "AmatsukazeCLI.hpp"
#include "LogoGUISupport.hpp"

// Avisynthフィルタデバッグ用
#include "TextOut.cpp"

HMODULE g_DllHandle;
bool g_av_initialized = false;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved) {
	if (dwReason == DLL_PROCESS_ATTACH) g_DllHandle = hModule;
	return TRUE;
}

extern "C" __declspec(dllexport) void InitAmatsukazeDLL()
{
	// FFMPEGライブラリ初期化
	av_register_all();
	avfilter_register_all();
}

static void init_console()
{
	AllocConsole();
	FILE* fp;
	freopen_s(&fp, "CONOUT$", "w", stdout);
	freopen_s(&fp, "CONIN$", "r", stdin);
}

// CM解析用（＋デバッグ用）インターフェース
extern "C" __declspec(dllexport) const char* __stdcall AvisynthPluginInit3(IScriptEnvironment* env, const AVS_Linkage* const vectors) {
	// 直接リンクしているのでvectorsを格納する必要はない

	if (g_av_initialized == false) {
		// FFMPEGライブラリ初期化
		av_register_all();
		avfilter_register_all();
		g_av_initialized = true;
	}

	env->AddFunction("AMTSource", "s[filter]s[outqp]b", av::CreateAMTSource, 0);

	env->AddFunction("AMTAnalyzeLogo", "cs[maskratio]i", logo::AMTAnalyzeLogo::Create, 0);
	env->AddFunction("AMTEraseLogo", "ccs[logof]s[mode]i", logo::AMTEraseLogo::Create, 0);

	return "Amatsukaze plugin";
}
