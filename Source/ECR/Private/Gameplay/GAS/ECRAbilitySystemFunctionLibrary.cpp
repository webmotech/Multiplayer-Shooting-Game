﻿#include "Gameplay/GAS/ECRAbilitySystemFunctionLibrary.h"
#include "GameplayCueFunctionLibrary.h"


struct FECRGameplayEffectContext;

FGameplayCueParameters UECRAbilitySystemFunctionLibrary::MakeGameplayCueParametersFromHitResultIncludingSource(
	const FHitResult& HitResult)
{
	FGameplayCueParameters CueParameters =
		UGameplayCueFunctionLibrary::MakeGameplayCueParametersFromHitResult(HitResult);
	CueParameters.SourceObject = HitResult.GetActor();
	return CueParameters;
}

FGameplayCueParameters UECRAbilitySystemFunctionLibrary::MakeGameplayCueParametersFromHitResultIncludingSourceAndCauser(
	const FHitResult& HitResult, AActor* Causer)
{
	FGameplayCueParameters CueParameters = MakeGameplayCueParametersFromHitResultIncludingSource(HitResult);
	CueParameters.EffectCauser = MakeWeakObjectPtr(Cast<AActor>(Causer));
	return CueParameters;
}

void UECRAbilitySystemFunctionLibrary::SetEffectContextSourceObject(FGameplayEffectContextHandle Handle, UObject* Object)
{
	Handle.AddSourceObject(Object);
}
