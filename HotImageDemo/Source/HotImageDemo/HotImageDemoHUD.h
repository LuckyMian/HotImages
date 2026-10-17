#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "HotImageDemoHUD.generated.h"

UCLASS()
class HOTIMAGEDEMO_API AHotImageDemoHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};
