#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "HotImageDemoGameMode.generated.h"

UCLASS()
class HOTIMAGEDEMO_API AHotImageDemoGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AHotImageDemoGameMode();

	virtual void BeginPlay() override;
};
