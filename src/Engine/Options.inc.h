// Don't worry about Intellisense errors here, as this file is only used in conjunction with Options.h/Options.cpp
// To add a new option, add a new variable entry and a corresponding OptionInfo in Options.cpp

// General options
OPT int displayWidth, displayHeight, maxFrameSkip, baseXResolution, baseYResolution, baseXGeoscape, baseYGeoscape, baseXBattlescape, baseYBattlescape,
	soundVolume, musicVolume, uiVolume, audioSampleRate, audioBitDepth, audioChunkSize, pauseMode, windowedModePositionX, windowedModePositionY, FPS, FPSInactive,
	changeValueByMouseWheel, dragScrollTimeTolerance, dragScrollPixelTolerance, mousewheelSpeed, autosaveFrequency;
OPT bool fullscreen, asyncBlit, playIntro, useScaleFilter, useHQXFilter, useXBRZFilter, useOpenGL, checkOpenGLErrors, vSyncForOpenGL, useOpenGLSmoothing,
	autosave, allowResize, borderless, debug, debugUi, fpsCounter, newSeedOnLoad, keepAspectRatio, nonSquarePixelRatio,
	cursorInBlackBandsInFullscreen, cursorInBlackBandsInWindow, cursorInBlackBandsInBorderlessWindow, maximizeInfoScreens, musicAlwaysLoop, StereoSound, verboseLogging, soldierDiaries, touchEnabled,
	rootWindowedMode, lazyLoadResources, backgroundMute, showCraftHangar;
OPT std::string language, useOpenGLShader;
OPT KeyboardType keyboardMode;
OPT SaveSort saveOrder;
OPT MusicFormat preferredMusic;
OPT SoundFormat preferredSound;
OPT VideoFormat preferredVideo;
OPT SDL_GrabMode captureMouse;
OPT TextWrapping wordwrap;
OPT SDLKey keyOk, keyCancel, keyScreenshot, keyFps, keyQuickLoad, keyQuickSave;

// Geoscape options
OPT int geoClockSpeed, dogfightSpeed, geoScrollSpeed, geoDragScrollButton, geoscapeScale;
OPT bool includePrimeStateInSavedLayout, anytimePsiTraining, weaponSelfDestruction, retainCorpses, craftLaunchAlways, dogfightAI,
	globeSurfaceCache, globeSeasons, globeDetail, globeRadarLines, globeFlightPaths, globeAllRadarsOnBaseBuild,
	storageLimitsEnforced, canSellLiveAliens, canTransferCraftsWhileAirborne, customInitialBase, aggressiveRetaliation, geoDragScrollInvert,
	allowBuildingQueue, showFundsOnGeoscape, psiStrengthEval, allowPsiStrengthImprovement, fieldPromotions, meetingPoint;
OPT SDLKey keyGeoLeft, keyGeoRight, keyGeoUp, keyGeoDown, keyGeoZoomIn, keyGeoZoomOut, keyGeoSpeed1, keyGeoSpeed2, keyGeoSpeed3, keyGeoSpeed4, keyGeoSpeed5, keyGeoSpeed6,
	keyGeoIntercept, keyGeoBases, keyGeoGraphs, keyGeoUfopedia, keyGeoOptions, keyGeoFunding, keyGeoToggleDetail, keyGeoToggleRadar,
	keyBaseSelect1, keyBaseSelect2, keyBaseSelect3, keyBaseSelect4, keyBaseSelect5, keyBaseSelect6, keyBaseSelect7, keyBaseSelect8;
OPT SDLKey keyAIList;	

// Battlescape options
OPT ScrollType battleEdgeScroll;
OPT PathPreview battleNewPreviewPath;
OPT int shootingSpreadMode, battleRealisticShotDispersion, battleRealisticCoverEfficiency, battleScrollSpeed, battleDragScrollButton, battleFireSpeed, battleXcomSpeed, battleAlienSpeed, battleExplosionHeight, battlescapeScale, battleTerrainSquishyness, preprimeGrenades, battleThrownSpeed;
OPT bool traceAI, battleInstantGrenade, battleNotifyDeath, battleTooltips, battleHairBleach, battleAutoEnd,
	strafe, forceFire, showMoreStatsInInventoryView, allowPsionicCapture, skipNextTurnScreen, disableAutoEquip, battleDragScrollInvert,
	battleUFOExtenderAccuracy, useChanceToHit, battleRealisticAccuracy, battleRealisticImprovedAimed, battleRealisticImprovedLof, battleRealisticDisplayRolls, battleAltGrenades, battleConfirmFireMode, battleSmoothCamera, noAlienPanicMessages, alienBleeding, instantPrime, strictBlockedChecking, updateTurnsSinceSeenByClue;
OPT SDLKey keyBattleLeft, keyBattleRight, keyBattleUp, keyBattleDown, keyBattleLevelUp, keyBattleLevelDown, keyBattleCenterUnit, keyBattlePrevUnit, keyBattleNextUnit, keyBattleDeselectUnit,
keyBattleUseLeftHand, keyBattleUseRightHand, keyBattleInventory, keyBattleMap, keyBattleOptions, keyBattleEndTurn, keyBattleAbort, keyBattleStats, keyBattleKneel,
keyBattleReserveKneel, keyBattleReload, keyBattlePersonalLighting, keyBattleReserveNone, keyBattleReserveSnap, keyBattleReserveAimed, keyBattleReserveAuto,
keyBattleCenterEnemy1, keyBattleCenterEnemy2, keyBattleCenterEnemy3, keyBattleCenterEnemy4, keyBattleCenterEnemy5, keyBattleCenterEnemy6, keyBattleCenterEnemy7, keyBattleCenterEnemy8,
keyBattleCenterEnemy9, keyBattleCenterEnemy10, keyBattleVoxelView, keyBattleZeroTUs, keyInvCreateTemplate, keyInvApplyTemplate, keyInvClear, keyInvAutoEquip;

// AI options
OPT bool sneakyAI, brutalAI, brutalCivilians, ignoreDelay, allowPreprime, autoCombat, aiPerformanceOptimization, avoidMines;
OPT int aiCheatMode;
OPT bool autoCombatEachCombat, autoCombatEachTurn, autoCombatControlPerUnit;
OPT bool autoCombatDefaultSoldier, autoCombatDefaultHWP, autoCombatDefaultMindControl, autoCombatDefaultRemain;

// Extra hotkeys (OXCE)
OPT SDLKey keyGeoDailyPilotExperience, keyGeoUfoTracker, keyGeoTechTreeViewer, keyGeoGlobalResearch, keyGeoGlobalProduction, keyGeoGlobalAlienContainment,
	keyGraphsZoomIn, keyGraphsZoomOut,
	keyToggleQuickSearch, keyInstaSave,
	keyCraftLoadoutSave, keyCraftLoadoutLoad,
	keyMarkAllAsSeen,
	keySellAll, keySellAllButOne,
	keyTransferAll,
	keyRemoveSoldiersFromCraft, keyRemoveSoldiersFromAllCrafts,
	keyRemoveEquipmentFromCraft,
	keyRemoveArmorFromCraft, keyRemoveArmorFromAllCrafts,
	keyRemoveSoldiersFromTraining, keyAddSoldiersToTraining,
	keyInventoryArmor, keyInventoryAvatar, keyInventoryDiaryLight, keyInventorySave, keyInventoryLoad,
	keyInvSavePersonalEquipment, keyInvLoadPersonalEquipment, keyInvShowPersonalEquipment,
	keyBattleUnitUp, keyBattleUnitDown,
	keyBattleShowLayers,
	keyBattleUseSpecial,
	keyBattleActionItem1, keyBattleActionItem2, keyBattleActionItem3, keyBattleActionItem4, keyBattleActionItem5,
	keyNightVisionToggle, keyNightVisionHold, keySelectMusicTrack, keyToggleAutoPlay;
OPT SDLKey keyBasescapeBuildNewBase, keyBasescapeBaseInfo, keyBasescapeSoldiers, keyBasescapeCrafts,
	keyBasescapeFacilities, keyBasescapeResearch, keyBasescapeManufacture, keyBasescapeTransfer,
	keyBasescapePurchase, keyBasescapeSell;

// OXCE, accessible via GUI
OPT bool oxceUpdateCheck;
OPT int autosaveSlots;
OPT int oxceGeoAutosaveFrequency;
OPT int oxceGeoAutosaveSlots;
OPT bool oxceLinks;
OPT bool oxceLinksDisableTextEdit;
OPT bool oxceFatFingerLinks;
OPT bool oxceBattleTouchButtonsEnabled;
OPT bool oxceQuickSearchButton;
OPT bool oxceHighlightNewTopics;
OPT bool oxcePediaShowClipSize;

OPT int oxceInterceptTableSize;
OPT bool oxceEnableSlackingIndicator;
OPT int oxceInterceptGuiMaintenanceTime;
OPT int oxceShowETAMode;
OPT bool oxceUfoLandingAlert;
OPT bool oxceRememberDisabledCraftWeapons;
OPT bool oxceGeoscapeEventsInstantDelivery;
OPT bool oxceShowBaseNameInPopups;

OPT bool oxceAlternateCraftEquipmentManagement;
OPT bool oxceBaseInfoScaleEnabled;
OPT int oxceResearchScrollSpeed;
OPT int oxceResearchScrollSpeedWithCtrl;
OPT bool oxceManufactureFilterSuppliesOK;
OPT int oxceManufactureScrollSpeed;
OPT int oxceManufactureScrollSpeedWithCtrl;
OPT bool oxcePersonalLayoutIncludingArmor;
OPT bool oxceManualPromotions;

OPT int oxceWoundedDefendBaseIf;
OPT bool oxcePlayBriefingMusicDuringEquipment;
OPT int oxceNightVisionColor;
OPT int oxceAutoNightVisionThreshold;
OPT int oxceFOW;
OPT int oxceFOWColor;
OPT int oxceShowAccuracyOnCrosshair;
OPT int oxceReactionFireThreshold;
OPT bool oxceAutoSell;
OPT bool oxceAutomaticPromotions;
OPT bool oxceEnableOffCentreShooting;
OPT bool oxceSmartCtrlEquip;

OPT bool oxceHighlightNewTopicsHidden;
OPT int oxceInterceptGuiMaintenanceTimeHidden;
// 0 = not persisted; 1 = persisted per battle; 2 = persisted per campaign
OPT int maxNumberOfBases;
OPT int oxceCrashedOrLanded;

/**
 * Verification level of mod data.
 * Same levels supported as `SeverityLevel`.
 */
OPT int oxceModValidationLevel;
OPT bool oxceRawScreenShots;
OPT bool oxceFirstPersonViewFisheyeProjection;
OPT bool oxceThumbButtons;
OPT int oxceThrottleMouseMoveEvent;
OPT bool oxceDisableThinkingProgressBar;

OPT bool oxceEmbeddedOnly;
OPT bool oxceListVFSContents;
OPT bool oxceEnablePaletteFlickerFix;
OPT bool oxceRecommendedOptionsWereSet;
OPT std::string password;

// OXCE hidden, but moddable via fixedUserOptions and/or recommendedUserOptions
OPT int oxceStartUpTextMode;

OPT int oxceGeoscapeDebugLogMaxEntries;
OPT int oxceGeoSlowdownFactor;
OPT bool oxceGeoShowScoreInsteadOfFunds;
OPT bool oxceGeoEnableTrainingIndicator;
OPT bool oxceGeoSuppressRedundantHKAlert;
OPT bool oxceGeoSuppressLandingWithoutEquipment;
OPT bool oxceGeoGoToNearestBase;
OPT bool oxceGeoSortCraftByDistanceToTarget;

OPT int oxceBaseInfoDefenseScaleMultiplier;
OPT bool oxceBaseSoldierTransformationShowOnlyEligible;
OPT bool oxceBaseFilterResearchable;
OPT bool oxceBaseManufactureFallbackButton;
OPT bool oxceBaseManufactureInfinityButton;
OPT bool oxceBaseTouchButtons;

OPT bool oxceDisableAlienInventory;
OPT bool oxceDisableHitLog;
OPT bool oxceDisableInventoryTuCost;
OPT bool oxceDisableProductionDependencyTree;
OPT bool oxceDisableStatsForNerds;
OPT bool oxceDisableTechTreeViewer;

// 0 = not persisted; 1 = persisted per battle; 2 = persisted per campaign
OPT int oxceTogglePersonalLightType;
OPT int oxceToggleNightVisionType;
OPT int oxceToggleBrightnessType;

OPT bool oxceShowBurningAsWounded;
OPT bool oxceDisableInfoOnThrowCursor;
OPT bool oxceSwapDontReselectActions;
OPT bool oxceEnableUnitResponseSounds;
OPT int oxceHiddenMovementBackgroundChangeFrequency;
OPT bool oxceInventoryShowUnitSlot;
OPT bool oxceReplaceNotesLink;

OPT bool oxceInventorySplitScrollButton;
OPT bool oxceInventoryDropItemOverPaperdoll;

OPT int oxceMaxEquipmentLayoutTemplates;

// Flags and other stuff that don't need OptionInfo's.
OPT bool mute, reload, newOpenGL, newScaleFilter, newHQXFilter, newXBRZFilter, newRootWindowedMode, newFullscreen, newAllowResize, newBorderless;
OPT int newDisplayWidth, newDisplayHeight, newBattlescapeScale, newGeoscapeScale, newWindowedModePositionX, newWindowedModePositionY;
OPT std::string newOpenGLShader;
OPT std::vector< std::pair<std::string, bool> > mods; // ordered list of available mods (lowest priority to highest) and whether they are active
OPT SoundFormat currentSound;

// hidden alien activity
OPT int displayHiddenAlienActivity;

// display base defense strength as a chance to win in base info screen
OPT bool baseDefenseProbability;

// display base detection probability in base info screen
OPT bool baseDetectionProbability;

// prime grenades
OPT SDLKey keyReadyLightGrenade;
OPT SDLKey keyReadyHeavyGrenade;
OPT SDLKey keyReadyProximityGrenade;
OPT SDLKey keyReadySmokeGrenade;
OPT SDLKey keyReadyStunGrenade;
OPT SDLKey keyReadyFlare;
OPT SDLKey keyReadyScanner;
OPT SDLKey keyReadyMedikit;
OPT SDLKey keyClearLeftHand;

OPT int battleXcomSpeedOrig;
