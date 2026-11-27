#pragma once

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

namespace WS2PackageTrace
{
	/** Appends one acceptance line to the path passed with -ws2trace=<file>. */
	inline void Write(const FString& Line)
	{
		FString Path;
		if (!FParse::Value(FCommandLine::Get(), TEXT("ws2trace="), Path) || Path.IsEmpty())
		{
			return;
		}

		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		const FString Prefix = FString::Printf(TEXT("[%s pid=%d pkg=%s] "),
			*FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S")),
			FPlatformProcess::GetCurrentProcessId(), *FPaths::ProjectDir());
		FString Out = Prefix + Line;
		Out.Append(LINE_TERMINATOR);
		if (!FFileHelper::SaveStringToFile(Out, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
			&IFileManager::Get(), EFileWrite::FILEWRITE_Append))
		{
			const FString Fallback = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WS2PackageTrace.log"));
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Fallback), true);
			FFileHelper::SaveStringToFile(Out, *Fallback, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
				&IFileManager::Get(), EFileWrite::FILEWRITE_Append);
		}
	}
}
