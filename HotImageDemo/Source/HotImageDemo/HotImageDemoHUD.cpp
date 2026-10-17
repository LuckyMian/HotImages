#include "HotImageDemoHUD.h"

#include "Engine/Texture2D.h"
#include "HotImagesSubsystem.h"

void AHotImageDemoHUD::DrawHUD()
{
	Super::DrawHUD();

	UHotImageSubsystem* Subsystem = GetGameInstance() ? GetGameInstance()->GetSubsystem<UHotImageSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return;
	}

	const FString SampleSlots[] = { TEXT("sample_1"), TEXT("sample_2"), TEXT("sample_3") };
	const FLinearColor Tints[] = { FLinearColor::White, FLinearColor::White, FLinearColor::White };

	float X = 40.f;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(SampleSlots); ++Index)
	{
		UTexture2D* Texture = Subsystem->GetTexture(SampleSlots[Index]);
		if (!Texture)
		{
			continue;
		}

		const float Size = 220.f;
		DrawTexture(Texture, X, 40.f, Size, Size, 0.f, 0.f, 1.f, 1.f, Tints[Index], BLEND_Translucent, 1.f, false);
		X += Size + 24.f;
	}

	DrawText(TEXT("HotImages demo: replace the files in Content/HotImages and watch the textures update."),
		FLinearColor::White, 40.f, 40.f + 240.f, nullptr, 1.2f, false);
}
