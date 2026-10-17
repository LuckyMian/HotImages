#include "HotImageDemoGameMode.h"
#include "Engine/GameInstance.h"
#include "HotImagesSubsystem.h"
#include "HotImageDemoHUD.h"

AHotImageDemoGameMode::AHotImageDemoGameMode()
{
	HUDClass = AHotImageDemoHUD::StaticClass();
}

void AHotImageDemoGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Force the sample proxy textures to load so runtime refresh can be exercised
	// even on a level that does not render them.
	if (UHotImageSubsystem* Subsystem = GetGameInstance()->GetSubsystem<UHotImageSubsystem>())
	{
		Subsystem->GetTexture(TEXT("sample_1"));
		Subsystem->GetTexture(TEXT("sample_2"));
		Subsystem->GetTexture(TEXT("sample_3"));
	}
}
