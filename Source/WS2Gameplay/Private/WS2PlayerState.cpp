#include "WS2PlayerState.h"

#include "Net/UnrealNetwork.h"

AWS2PlayerState::AWS2PlayerState()
{
	bReplicates = true;
}

void AWS2PlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWS2PlayerState, Slot);
	DOREPLIFETIME(AWS2PlayerState, HeroId);
	DOREPLIFETIME(AWS2PlayerState, HeroName);
}
