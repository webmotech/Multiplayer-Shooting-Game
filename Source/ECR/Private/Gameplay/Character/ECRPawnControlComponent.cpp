﻿// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Character/ECRPawnControlComponent.h"
#include "System/ECRLogChannels.h"
#include "GameFramework/Pawn.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Gameplay/Player/ECRPlayerController.h"
#include "Gameplay/Player/ECRPlayerState.h"
#include "Gameplay/Character/ECRPawnExtensionComponent.h"
#include "Gameplay/Character/ECRPawnData.h"
#include "CoreExtendingFunctionLibrary.h"
#include "Gameplay/GAS/ECRAbilitySystemComponent.h"
#include "Input/ECRInputConfig.h"
#include "Input/ECRInputComponent.h"
#include "Gameplay/Camera/ECRCameraComponent.h"
#include "Gameplay/ECRGameplayTags.h"
#include "Engine/LocalPlayer.h"
#include "Components/GameFrameworkComponentManager.h"
#include "Settings/ECRSettingsLocal.h"
#include "PlayerMappableInputConfig.h"
#include "Gameplay/Camera/ECRCameraMode.h"

#if WITH_EDITOR
#include "Misc/UObjectToken.h"
#endif	// WITH_EDITOR

namespace ECRHero
{
	static constexpr float LookYawRate = 300.0f;
	static constexpr float LookPitchRate = 165.0f;
};

const FName UECRPawnControlComponent::NAME_BindInputsNow("BindInputsNow");

UECRPawnControlComponent::UECRPawnControlComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	AbilityCameraMode = nullptr;
	bPawnHasInitialized = false;
	bReadyToBindInputs = false;
	bMovementInputEnabled = true;

	bListenForAbilityQueue = false;
	AbilityQueueDeltaTime = 0;

	LookPitchLimit = 180.0f;
	LookYawLimit = 180.0f;
}

void UECRPawnControlComponent::OnRegister()
{
	Super::OnRegister();

	if (const APawn* Pawn = GetPawn<APawn>())
	{
		if (UECRPawnExtensionComponent* PawnExtComp = UECRPawnExtensionComponent::FindPawnExtensionComponent(Pawn))
		{
			PawnExtComp->OnPawnReadyToInitialize_RegisterAndCall(
				FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &ThisClass::OnPawnReadyToInitialize));
		}
	}
	else
	{
		UE_LOG(LogECR, Error,
		       TEXT(
			       "[UECRPawnControlComponent::OnRegister] This component has been added to a blueprint whose base class is not a Pawn. To use this component, it MUST be placed on a Pawn Blueprint."
		       ));

#if WITH_EDITOR
		if (GIsEditor)
		{
			static const FText Message = NSLOCTEXT("ECRHeroComponent", "NotOnPawnError",
			                                       "has been added to a blueprint whose base class is not a Pawn. To use this component, it MUST be placed on a Pawn Blueprint. This will cause a crash if you PIE!");
			static const FName HeroMessageLogName = TEXT("ECRHeroComponent");

			FMessageLog(HeroMessageLogName).Error()
			                               ->AddToken(FUObjectToken::Create(this, FText::FromString(GetNameSafe(this))))
			                               ->AddToken(FTextToken::Create(Message));

			FMessageLog(HeroMessageLogName).Open();
		}
#endif
	}
}

bool UECRPawnControlComponent::IsPawnComponentReadyToInitialize() const
{
	// The player state is required.
	if (!GetPlayerState<AECRPlayerState>())
	{
		return false;
	}

	const APawn* Pawn = GetPawn<APawn>();

	// A pawn is required.
	if (!Pawn)
	{
		return false;
	}

	// If we're authority or autonomous, we need to wait for a controller with registered ownership of the player state.
	if (Pawn->GetLocalRole() != ROLE_SimulatedProxy)
	{
		AController* Controller = GetController<AController>();

		const bool bHasControllerPairedWithPS = (Controller != nullptr) &&
			(Controller->PlayerState != nullptr) &&
			(Controller->PlayerState->GetOwner() == Controller);

		if (!bHasControllerPairedWithPS)
		{
			return false;
		}
	}

	const bool bIsLocallyControlled = Pawn->IsLocallyControlled();
	const bool bIsBot = Pawn->IsBotControlled();

	if (bIsLocallyControlled && !bIsBot)
	{
		// The input component is required when locally controlled.
		if (!Pawn->InputComponent)
		{
			return false;
		}
	}

	return true;
}

void UECRPawnControlComponent::InitInputAndCamera()
{
	APawn* Pawn = GetPawn<APawn>();
	if (!Pawn)
	{
		return;
	}
	const bool bIsLocallyControlled = Pawn->IsLocallyControlled();

	// Init input
	if (GetController<AECRPlayerController>())
	{
		if (Pawn->InputComponent != nullptr)
		{
			InitializePlayerInput(Pawn->InputComponent);
		}
	}

	const UECRPawnData* PawnData = nullptr;

	if (const UECRPawnExtensionComponent* PawnExtComp = UECRPawnExtensionComponent::FindPawnExtensionComponent(Pawn))
	{
		PawnData = PawnExtComp->GetPawnData<UECRPawnData>();
	}

	if (bIsLocallyControlled && PawnData)
	{
		if (UECRCameraComponent* CameraComponent = UECRCameraComponent::FindCameraComponent(Pawn))
		{
			CameraComponent->DetermineCameraModeDelegate.BindUObject(this, &ThisClass::DetermineCameraMode);
		}
	}
}

void UECRPawnControlComponent::OnPawnReadyToInitialize()
{
	if (!ensure(!bPawnHasInitialized))
	{
		// Don't initialize twice
		return;
	}

	APawn* Pawn = GetPawn<APawn>();
	if (!Pawn)
	{
		return;
	}

	AECRPlayerState* ECRPS = GetPlayerState<AECRPlayerState>();
	check(ECRPS);

	if (UECRPawnExtensionComponent* PawnExtComp = UECRPawnExtensionComponent::FindPawnExtensionComponent(Pawn))
	{
		if (const IAbilitySystemInterface* AbilitySystemInterface = Cast<IAbilitySystemInterface>(Pawn))
		{
			if (UECRAbilitySystemComponent* EASC = Cast<UECRAbilitySystemComponent>(
				AbilitySystemInterface->GetAbilitySystemComponent()))
			{
				PawnExtComp->InitializeAbilitySystem(EASC, Pawn);
			}
			
		}
	}

	InitInputAndCamera();

	bPawnHasInitialized = true;
}

void UECRPawnControlComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UECRPawnControlComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const APawn* Pawn = GetPawn<APawn>())
	{
		if (UECRPawnExtensionComponent* PawnExtComp = UECRPawnExtensionComponent::FindPawnExtensionComponent(Pawn))
		{
			PawnExtComp->UninitializeAbilitySystem();
		}
	}

	Super::EndPlay(EndPlayReason);
}

void UECRPawnControlComponent::InitializePlayerInput(UInputComponent* PlayerInputComponent)
{
	check(PlayerInputComponent);

	const APawn* Pawn = GetPawn<APawn>();
	if (!Pawn)
	{
		return;
	}

	const APlayerController* PC = GetController<APlayerController>();
	check(PC);

	const ULocalPlayer* LP = PC->GetLocalPlayer();
	check(LP);

	UEnhancedInputLocalPlayerSubsystem* Subsystem = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	check(Subsystem);

	Subsystem->ClearAllMappings();

	if (const UECRPawnExtensionComponent* PawnExtComp = UECRPawnExtensionComponent::FindPawnExtensionComponent(Pawn))
	{
		if (const UECRPawnData* PawnData = PawnExtComp->GetPawnData<UECRPawnData>())
		{
			if (const UECRInputConfig* InputConfig = PawnData->InputConfig)
			{
				// Register any default input configs with the settings so that they will be applied to the player during AddInputMappings
				for (const FMappableConfigPair& Pair : DefaultInputConfigs)
				{
					if (Pair.bShouldActivateAutomatically && Pair.CanBeActivated())
					{
						FModifyContextOptions Options = {};
						Options.bIgnoreAllPressedKeysUntilRelease = false;
						// Actually add the config to the local player							
						Subsystem->AddPlayerMappableConfig(Pair.Config.LoadSynchronous(), Options);
					}
				}

				UECRInputComponent* ECRIC = CastChecked<UECRInputComponent>(PlayerInputComponent);
				ECRIC->AddInputMappings(InputConfig, Subsystem);

				TArray<uint32> BindHandles;
				ECRIC->BindAbilityActions(InputConfig, this, &ThisClass::Input_AbilityInputTagPressed,
				                          &ThisClass::Input_AbilityInputTagReleased, /*out*/ BindHandles);


				BindNativeActions(ECRIC, InputConfig);
			}
		}
	}

	if (ensure(!bReadyToBindInputs))
	{
		bReadyToBindInputs = true;
	}

	UGameFrameworkComponentManager::SendGameFrameworkComponentExtensionEvent(
		const_cast<APlayerController*>(PC), NAME_BindInputsNow);
	UGameFrameworkComponentManager::SendGameFrameworkComponentExtensionEvent(
		const_cast<APawn*>(Pawn), NAME_BindInputsNow);
}

void UECRPawnControlComponent::BindNativeActions(UECRInputComponent* ECRIC, const UECRInputConfig* InputConfig)
{
	const FECRGameplayTags& GameplayTags = FECRGameplayTags::Get();
	ECRIC->BindNativeAction(InputConfig, GameplayTags.InputTag_Look_Mouse, ETriggerEvent::Triggered, this,
	                        &ThisClass::Input_LookMouse, /*bLogIfNotFound=*/ true);
	ECRIC->BindNativeAction(InputConfig, GameplayTags.InputTag_Look_Stick, ETriggerEvent::Triggered, this,
	                        &ThisClass::Input_LookStick, /*bLogIfNotFound=*/ false);
}

void UECRPawnControlComponent::NotifyAbilityQueueSystem(UECRAbilitySystemComponent* ASC, const FGameplayTag& InputTag)
{
	if (InputTag.IsValid() && bListenForAbilityQueue && AbilityQueueInputTags.HasTagExact(InputTag))
	{
		ASC->AbilityQueueSystemLastInputTag = InputTag;
		ASC->AbilityQueueSystemLastInputTagTime = GetWorld()->GetTimeSeconds();
		ASC->AbilityQueueSystemDeltaTime = AbilityQueueDeltaTime;
	}
	else
	{
		ASC->AbilityQueueSystemLastInputTag = {};
		ASC->AbilityQueueSystemLastInputTagTime = 0;
		ASC->AbilityQueueSystemDeltaTime = 0;
	}
}


void UECRPawnControlComponent::AddAdditionalInputConfig(const UECRInputConfig* InputConfig)
{
	TArray<uint32> BindHandles;

	const APawn* Pawn = GetPawn<APawn>();
	if (!Pawn)
	{
		return;
	}

	UECRInputComponent* ECRIC = Pawn->FindComponentByClass<UECRInputComponent>();
	check(ECRIC);

	const APlayerController* PC = GetController<APlayerController>();
	check(PC);

	const ULocalPlayer* LP = PC->GetLocalPlayer();
	check(LP);

	UEnhancedInputLocalPlayerSubsystem* Subsystem = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	check(Subsystem);

	if (const UECRPawnExtensionComponent* PawnExtComp = UECRPawnExtensionComponent::FindPawnExtensionComponent(Pawn))
	{
		ECRIC->BindAbilityActions(InputConfig, this, &ThisClass::Input_AbilityInputTagPressed,
		                          &ThisClass::Input_AbilityInputTagReleased, /*out*/ BindHandles);
	}
}

void UECRPawnControlComponent::RemoveAdditionalInputConfig(const UECRInputConfig* InputConfig)
{
	//@TODO: Implement me!
}

bool UECRPawnControlComponent::HasPawnInitialized() const
{
	return bPawnHasInitialized;
}

bool UECRPawnControlComponent::IsReadyToBindInputs() const
{
	return bReadyToBindInputs;
}

void UECRPawnControlComponent::Input_AbilityInputTagPressed(FGameplayTag InputTag)
{
	if (const APawn* Pawn = GetPawn<APawn>())
	{
		if (const UECRPawnExtensionComponent* PawnExtComp =
			UECRPawnExtensionComponent::FindPawnExtensionComponent(Pawn))
		{
			if (UECRAbilitySystemComponent* ECRASC = PawnExtComp->GetECRAbilitySystemComponent())
			{
				ECRASC->AbilityInputTagPressed(InputTag);
				NotifyAbilityQueueSystem(ECRASC, InputTag);
			}
		}
	}
}

void UECRPawnControlComponent::Input_AbilityInputTagReleased(FGameplayTag InputTag)
{
	const APawn* Pawn = GetPawn<APawn>();
	if (!Pawn)
	{
		return;
	}

	if (const UECRPawnExtensionComponent* PawnExtComp = UECRPawnExtensionComponent::FindPawnExtensionComponent(Pawn))
	{
		if (UECRAbilitySystemComponent* ECRASC = PawnExtComp->GetECRAbilitySystemComponent())
		{
			ECRASC->AbilityInputTagReleased(InputTag);
		}
	}
}

void UECRPawnControlComponent::Input_LookMouse(const FInputActionValue& InputActionValue)
{
	APawn* Pawn = GetPawn<APawn>();

	if (!Pawn)
	{
		return;
	}

	const FVector2D Value = InputActionValue.Get<FVector2D>();

	double CurrentPitchDiff, CurrentYawDiff;
	UCoreExtendingFunctionLibrary::GetPawnAimOffsetDifference(Pawn, CurrentPitchDiff, CurrentYawDiff);

	if (Value.X != 0.0f)
	{
		if (LookYawLimit != 180.0f)
		{
			// Locked yaw, need to check if input is allowed
			if (FMath::Abs(CurrentYawDiff + Value.X) <= LookYawLimit)
			{
				Pawn->AddControllerYawInput(Value.X);
			}
		}
		else
		{
			Pawn->AddControllerYawInput(Value.X);
		}
	}

	if (Value.Y != 0.0f)
	{
		if (LookPitchLimit != 180.0f)
		{
			// Locked pitch, need to check if input is allowed
			if (FMath::Abs(CurrentPitchDiff + Value.Y) <= LookPitchLimit)
			{
				Pawn->AddControllerPitchInput(Value.Y);
			}
		}
		else
		{
			Pawn->AddControllerPitchInput(Value.Y);
		}
	}
}

void UECRPawnControlComponent::Input_LookStick(const FInputActionValue& InputActionValue)
{
	APawn* Pawn = GetPawn<APawn>();

	if (!Pawn)
	{
		return;
	}

	const FVector2D Value = InputActionValue.Get<FVector2D>();

	double CurrentPitchDiff, CurrentYawDiff;
	UCoreExtendingFunctionLibrary::GetPawnAimOffsetDifference(Pawn, CurrentPitchDiff, CurrentYawDiff);

	const UWorld* World = GetWorld();
	check(World);

	if (Value.X != 0.0f)
	{
		if (LookYawLimit != 180.0f)
		{
			// Locked yaw, need to check if input is allowed
			if (FMath::Abs(CurrentYawDiff + Value.X) <= LookYawLimit)
			{
				Pawn->AddControllerYawInput(Value.X);
			}
		}
		else
		{
			Pawn->AddControllerYawInput(Value.X);
		}
	}

	if (Value.Y != 0.0f)
	{
		if (LookPitchLimit != 180.0f)
		{
			// Locked pitch, need to check if input is allowed
			if (FMath::Abs(CurrentPitchDiff + Value.Y) <= LookPitchLimit)
			{
				Pawn->AddControllerPitchInput(Value.Y);
			}
		}
		else
		{
			Pawn->AddControllerPitchInput(Value.Y);
		}
	}
}


void UECRPawnControlComponent::ToggleMovementInput(const bool bNewEnabled)
{
	bMovementInputEnabled = bNewEnabled;
}

TSubclassOf<UECRCameraMode> UECRPawnControlComponent::DetermineCameraMode() const
{
	if (AbilityCameraMode)
	{
		return AbilityCameraMode;
	}

	const APawn* Pawn = GetPawn<APawn>();
	if (!Pawn)
	{
		return nullptr;
	}

	if (UECRPawnExtensionComponent* PawnExtComp = UECRPawnExtensionComponent::FindPawnExtensionComponent(Pawn))
	{
		if (const UECRPawnData* PawnData = PawnExtComp->GetPawnData<UECRPawnData>())
		{
			return PawnData->DefaultCameraMode;
		}
	}

	return nullptr;
}

void UECRPawnControlComponent::SetAbilityCameraMode(TSubclassOf<UECRCameraMode> CameraMode,
                                                    const FGameplayAbilitySpecHandle& OwningSpecHandle)
{
	if (CameraMode)
	{
		AbilityCameraMode = CameraMode;
		AbilityCameraModeOwningSpecHandle = OwningSpecHandle;
	}
}

void UECRPawnControlComponent::ClearAbilityCameraMode(const FGameplayAbilitySpecHandle& OwningSpecHandle)
{
	if (AbilityCameraModeOwningSpecHandle == OwningSpecHandle)
	{
		AbilityCameraMode = nullptr;
		AbilityCameraModeOwningSpecHandle = FGameplayAbilitySpecHandle();
	}
}
