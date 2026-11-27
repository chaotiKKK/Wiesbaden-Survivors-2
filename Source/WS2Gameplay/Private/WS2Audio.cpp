#include "WS2Audio.h"
#include "AudioDevice.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "CoreGlobals.h"
#include "WS2PackageTrace.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundWaveProcedural.h"

DEFINE_LOG_CATEGORY_STATIC(LogWS2Audio, Log, All);

namespace
{
	constexpr int32 SampleRate = 22050;

	struct FVoice
	{
		float StartFreq = 440.f;
		float EndFreq = 440.f;
		float Duration = 0.12f;
		float Noise = 0.f;
		float Volume = 0.5f;
		bool bSquare = false;
		bool bArpeggio = false;
	};

	FVoice VoiceFor(WS2Audio::ESfx Sfx)
	{
		switch (Sfx)
		{
		case WS2Audio::ESfx::Shot:     return { 980.f, 260.f, 0.075f, 0.28f, 0.32f, true, false };
		case WS2Audio::ESfx::Hit:      return { 300.f, 90.f, 0.055f, 0.42f, 0.30f, false, false };
		case WS2Audio::ESfx::Kill:     return { 640.f, 90.f, 0.17f, 0.30f, 0.36f, false, false };
		case WS2Audio::ESfx::Pickup:   return { 760.f, 1320.f, 0.14f, 0.04f, 0.34f, false, false };
		case WS2Audio::ESfx::LevelUp:  return { 523.f, 1046.f, 0.38f, 0.02f, 0.38f, false, true };
		case WS2Audio::ESfx::Wave:     return { 130.f, 260.f, 0.48f, 0.06f, 0.34f, false, false };
		case WS2Audio::ESfx::Boss:     return { 58.f, 36.f, 0.95f, 0.16f, 0.42f, false, false };
		case WS2Audio::ESfx::Down:     return { 220.f, 55.f, 0.55f, 0.12f, 0.38f, false, false };
		case WS2Audio::ESfx::Shop:     return { 660.f, 990.f, 0.22f, 0.02f, 0.30f, false, false };
		case WS2Audio::ESfx::Win:      return { 523.f, 1046.f, 0.70f, 0.02f, 0.45f, false, true };
		default:                       return {};
		}
	}

	void GeneratePcm(WS2Audio::ESfx Sfx, TArray<int16>& OutPcm)
	{
		const FVoice Voice = VoiceFor(Sfx);
		const int32 Samples = FMath::Max(1, FMath::RoundToInt(Voice.Duration * SampleRate));
		OutPcm.SetNumUninitialized(Samples);

		float Phase = 0.f;
		for (int32 Index = 0; Index < Samples; ++Index)
		{
			const float T = Index / static_cast<float>(SampleRate);
			const float Alpha = Index / static_cast<float>(Samples);
			float Frequency = FMath::Lerp(Voice.StartFreq, Voice.EndFreq, Alpha);
			if (Voice.bArpeggio)
			{
				const int32 Step = FMath::Clamp(FMath::FloorToInt(Alpha * 4.f), 0, 3);
				static const float Ratios[] = { 1.f, 1.25f, 1.5f, 2.f };
				Frequency = Voice.StartFreq * Ratios[Step];
			}
			Phase += 2.f * PI * Frequency / SampleRate;

			const float Attack = FMath::Min(1.f, T * 70.f);
			const float Release = FMath::Pow(FMath::Max(0.f, 1.f - Alpha), 1.35f);
			const float Envelope = Attack * Release;
			const float Tone = Voice.bSquare
				? (FMath::Sin(Phase) >= 0.f ? 1.f : -1.f)
				: FMath::Sin(Phase) + 0.22f * FMath::Sin(Phase * 2.f);
			const float Noise = FMath::Sin(T * 9173.f) * FMath::Sin(T * 2311.f);
			const float Sample = FMath::Clamp((Tone * 0.78f + Noise * Voice.Noise) * Envelope * Voice.Volume, -1.f, 1.f);
			OutPcm[Index] = static_cast<int16>(Sample * 32767.f);
		}
	}

	const TCHAR* SfxName(WS2Audio::ESfx Sfx)
	{
		switch (Sfx)
		{
		case WS2Audio::ESfx::Shot: return TEXT("shot");
		case WS2Audio::ESfx::Hit: return TEXT("hit");
		case WS2Audio::ESfx::Kill: return TEXT("kill");
		case WS2Audio::ESfx::Pickup: return TEXT("pickup");
		case WS2Audio::ESfx::LevelUp: return TEXT("levelup");
		case WS2Audio::ESfx::Wave: return TEXT("wave");
		case WS2Audio::ESfx::Boss: return TEXT("boss");
		case WS2Audio::ESfx::Down: return TEXT("down");
		case WS2Audio::ESfx::Shop: return TEXT("shop");
		case WS2Audio::ESfx::Win: return TEXT("win");
		default: return TEXT("unknown");
		}
	}
}

void WS2Audio::Play(UWorld* World, ESfx Sfx, const FVector& Location, float Volume, const AActor* Source)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	const double Now = FPlatformTime::Seconds();
	static TMap<uint8, double> LastPlayed;
	const double MinInterval = Sfx == ESfx::Shot ? 0.065 : (Sfx == ESfx::Hit ? 0.055 : 0.035);
	const uint8 Key = static_cast<uint8>(Sfx);
	if (const double* Last = LastPlayed.Find(Key))
	{
		if (Now - *Last < MinInterval)
		{
			return;
		}
	}
	LastPlayed.Add(Key, Now);

	TArray<int16> Pcm;
	GeneratePcm(Sfx, Pcm);
	if (Pcm.Num() == 0)
	{
		return;
	}

	USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>();
	Wave->SetSampleRate(SampleRate);
	Wave->NumChannels = 1;
	Wave->Duration = VoiceFor(Sfx).Duration;
	Wave->SoundGroup = SOUNDGROUP_Default;
	Wave->bLooping = false;
	Wave->QueueAudio(reinterpret_cast<const uint8*>(Pcm.GetData()), Pcm.Num() * sizeof(int16));

	static TSet<uint8> Logged;
	if (!Logged.Contains(Key))
	{
		Logged.Add(Key);
		UE_LOG(LogWS2Audio, Display, TEXT("procedural SFX ready: type=%d samples=%d"), Key, Pcm.Num());
		WS2PackageTrace::Write(FString::Printf(TEXT("procedural SFX ready: type=%d samples=%d"), Key, Pcm.Num()));
	}

	const float SafeVolume = FMath::Clamp(Volume, 0.f, 1.f);
	if (Location.IsNearlyZero())
	{
		UGameplayStatics::PlaySound2D(World, Wave, SafeVolume);
	}
	else
	{
		UGameplayStatics::PlaySoundAtLocation(World, Wave, Location, SafeVolume);
	}

	FAudioDevice* AudioDevice = GEngine ? GEngine->GetMainAudioDeviceRaw() : nullptr;
	const bool bSoundEnabled = GEngine && GEngine->UseSound();
	const int32 ActiveSources = AudioDevice ? AudioDevice->GetNumActiveSources() : 0;
	const double AudioTime = AudioDevice ? AudioDevice->GetAudioTime() : 0.0;
	const bool bDeviceMuted = AudioDevice && AudioDevice->IsAudioDeviceMuted();
	TArray<FString> OutputDevices;
	if (AudioDevice)
	{
		AudioDevice->GetAudioDeviceList(OutputDevices);
	}
	const bool bChannelOpen = bSoundEnabled && AudioDevice && (AudioTime > 0.0 || ActiveSources > 0);
	const FString SourceName = Source ? Source->GetName() : TEXT("world");
	WS2PackageTrace::Write(FString::Printf(
		TEXT("AUDIO TRIGGER sfx=%s type=%d frame=%llu source=%s loc=%.1f,%.1f,%.1f volume=%.2f channelOpen=%d audioDevice=1 soundEnabled=%d outputDevices=%d audioTime=%.3f activeSources=%d muted=%d"),
		SfxName(Sfx), static_cast<int32>(Key), static_cast<uint64>(GFrameCounter), *SourceName,
		Location.X, Location.Y, Location.Z, SafeVolume, bChannelOpen ? 1 : 0,
		bSoundEnabled ? 1 : 0, OutputDevices.Num(), AudioTime, ActiveSources, bDeviceMuted ? 1 : 0));
}