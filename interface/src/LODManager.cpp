//
//  LODManager.cpp
//  interface/src/LODManager.h
//
//  Created by Clement on 1/16/15.
//  Copyright 2015 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <avatar/AvatarManager.h>
#include <SettingHandle.h>
#include <Util.h>

#include "Application.h"
#include "ui/DialogsManager.h"
#include "InterfaceLogging.h"

#include "LODManager.h"

Setting::Handle<float> desktopLODDecreaseFPS("desktopLODDecreaseFPS", DEFAULT_DESKTOP_LOD_DOWN_FPS);
Setting::Handle<float> hmdLODDecreaseFPS("hmdLODDecreaseFPS", DEFAULT_HMD_LOD_DOWN_FPS);
// There are two different systems in use, based on lodPreference:
// pid: renderDistance is adjusted by a PID such that frame rate targets are met.
// acuity:  a pseudo-acuity target is held, or adjusted to match minimum frame rates (and a PID controlls avatar rendering distance)
// If unspecified, acuity is used only if user has specified non-default minumum frame rates.
Setting::Handle<int> lodPreference("lodPreference", (int)LODManager::LODPreference::unspecified);
const float SMALLEST_REASONABLE_HORIZON = 50.0f; // meters
Setting::Handle<float> renderDistanceInverseHighLimit("renderDistanceInverseHighLimit", 1.0f / SMALLEST_REASONABLE_HORIZON);
void LODManager::setRenderDistanceInverseHighLimit(float newValue) {
    renderDistanceInverseHighLimit.set(newValue); // persist it, and tell all the controllers that use it
    _renderDistanceController.setControlledValueHighLimit(newValue);
}

LODManager::LODManager() {
    calculateAvatarLODDistanceMultiplier();

    setRenderDistanceInverseHighLimit(renderDistanceInverseHighLimit.get());
    setRenderDistanceInverseLowLimit(1.0f / (float)TREE_SCALE);
    // Advice for tuning parameters:
    // See PIDController.h. There's a section on tuning in the reference.
    // Turn on logging with the following (or from js with AvatarList.setRenderDistanceControllerHistory("avatar render", 300))
    //_renderDistanceController.setHistorySize("avatar render", target_fps * 4);
    // Note that extra logging/hysteresis is turned off in Avatar.cpp when the above logging is on.
    setRenderDistanceKP(0.000012f); // Usually about 0.6 of largest that doesn't oscillate when other parameters 0.
    setRenderDistanceKI(0.00002f); // Big enough to bring us to target with the above KP.
    //setRenderDistanceControllerHistory("FIXME", 240);
}

float LODManager::getLODDecreaseFPS() {
    if (qApp->isHMDMode()) {
        return getHMDLODDecreaseFPS();
    }
    return getDesktopLODDecreaseFPS();
}

float LODManager::getLODIncreaseFPS() {
    if (qApp->isHMDMode()) {
        return getHMDLODIncreaseFPS();
    }
    return getDesktopLODIncreaseFPS();
}

void LODManager::autoAdjustLOD(float currentFPS) {
    
    // NOTE: our first ~100 samples at app startup are completely all over the place, and we don't
    // really want to count them in our average, so we will ignore the real frame rates and stuff
    // our moving average with simulated good data
    const int IGNORE_THESE_SAMPLES = 100;
    if (_fpsAverageUpWindow.getSampleCount() < IGNORE_THESE_SAMPLES) {
        currentFPS = ASSUMED_FPS;
        _lastStable = _lastUpShift = _lastDownShift = usecTimestampNow();
    }
    
    _fpsAverageStartWindow.updateAverage(currentFPS);
    _fpsAverageDownWindow.updateAverage(currentFPS);
    _fpsAverageUpWindow.updateAverage(currentFPS);
    
    quint64 now = usecTimestampNow();

    bool changed = false;
    quint64 elapsedSinceDownShift = now - _lastDownShift;
    quint64 elapsedSinceUpShift = now - _lastUpShift;

    quint64 lastStableOrUpshift = glm::max(_lastUpShift, _lastStable);
    quint64 elapsedSinceStableOrUpShift = now - lastStableOrUpshift;
    
    if (_automaticLODAdjust) {
    
        // LOD Downward adjustment 
        // If we've been downshifting, we watch a shorter downshift window so that we will quickly move toward our
        // target frame rate. But if we haven't just done a downshift (either because our last shift was an upshift,
        // or because we've just started out) then we look at a much longer window to consider whether or not to start
        // downshifting.
        bool doDownShift = false; 

        if (_isDownshifting) {
            // only consider things if our DOWN_SHIFT time has elapsed...
            if (elapsedSinceDownShift > DOWN_SHIFT_ELPASED) {
                doDownShift = _fpsAverageDownWindow.getAverage() < getLODDecreaseFPS();
                
                if (!doDownShift) {
                    qCDebug(interfaceapp) << "---- WE APPEAR TO BE DONE DOWN SHIFTING -----";
                    _isDownshifting = false;
                    _lastStable = now;
                }
            }
        } else {
            doDownShift = (elapsedSinceStableOrUpShift > START_SHIFT_ELPASED 
                                && _fpsAverageStartWindow.getAverage() < getLODDecreaseFPS());
        }
        
        if (doDownShift) {

            // Octree items... stepwise adjustment
            if (_octreeSizeScale > ADJUST_LOD_MIN_SIZE_SCALE) {
                _octreeSizeScale *= ADJUST_LOD_DOWN_BY;
                if (_octreeSizeScale < ADJUST_LOD_MIN_SIZE_SCALE) {
                    _octreeSizeScale = ADJUST_LOD_MIN_SIZE_SCALE;
                }
                changed = true;
            }

            if (changed) {
                if (_isDownshifting) {
                    // subsequent downshift
                    qCDebug(interfaceapp) << "adjusting LOD DOWN..."
                                << "average fps for last "<< DOWN_SHIFT_WINDOW_IN_SECS <<"seconds was " 
                                << _fpsAverageDownWindow.getAverage() 
                                << "minimum is:" << getLODDecreaseFPS() 
                                << "elapsedSinceDownShift:" << elapsedSinceDownShift
                                << " NEW _octreeSizeScale=" << _octreeSizeScale;
                } else {
                    // first downshift
                    qCDebug(interfaceapp) << "adjusting LOD DOWN after initial delay..."
                                << "average fps for last "<< START_DELAY_WINDOW_IN_SECS <<"seconds was " 
                                << _fpsAverageStartWindow.getAverage() 
                                << "minimum is:" << getLODDecreaseFPS() 
                                << "elapsedSinceUpShift:" << elapsedSinceUpShift
                                << " NEW _octreeSizeScale=" << _octreeSizeScale;
                }

                _lastDownShift = now;
                _isDownshifting = true;

                emit LODDecreased();
            }
        } else {
    
            // LOD Upward adjustment
            if (elapsedSinceUpShift > UP_SHIFT_ELPASED) {
            
                if (_fpsAverageUpWindow.getAverage() > getLODIncreaseFPS()) {

                    // Octee items... stepwise adjustment
                    if (_octreeSizeScale < ADJUST_LOD_MAX_SIZE_SCALE) {
                        if (_octreeSizeScale < ADJUST_LOD_MIN_SIZE_SCALE) {
                            _octreeSizeScale = ADJUST_LOD_MIN_SIZE_SCALE;
                        } else {
                            _octreeSizeScale *= ADJUST_LOD_UP_BY;
                        }
                        if (_octreeSizeScale > ADJUST_LOD_MAX_SIZE_SCALE) {
                            _octreeSizeScale = ADJUST_LOD_MAX_SIZE_SCALE;
                        }
                        changed = true;
                    }
                }
        
                if (changed) {
                    qCDebug(interfaceapp) << "adjusting LOD UP... average fps for last "<< UP_SHIFT_WINDOW_IN_SECS <<"seconds was " 
                                << _fpsAverageUpWindow.getAverage()
                                << "upshift point is:" << getLODIncreaseFPS() 
                                << "elapsedSinceUpShift:" << elapsedSinceUpShift
                                << " NEW _octreeSizeScale=" << _octreeSizeScale;

                    _lastUpShift = now;
                    _isDownshifting = false;

                    emit LODIncreased();
                }
            }
        }
    
        if (changed) {
            calculateAvatarLODDistanceMultiplier();
            _shouldRenderTableNeedsRebuilding = true;
            auto lodToolsDialog = DependencyManager::get<DialogsManager>()->getLodToolsDialog();
            if (lodToolsDialog) {
                lodToolsDialog->reloadSliders();
            }
        }
    }
}

void LODManager::resetLODAdjust() {
    _fpsAverageStartWindow.reset();
    _fpsAverageDownWindow.reset();
    _fpsAverageUpWindow.reset();
    _lastUpShift = _lastDownShift = usecTimestampNow();
    _isDownshifting = false;
}

QString LODManager::getLODFeedbackText() {
    // determine granularity feedback
    int boundaryLevelAdjust = getBoundaryLevelAdjust();
    QString granularityFeedback;
    
    switch (boundaryLevelAdjust) {
        case 0: {
            granularityFeedback = QString(".");
        } break;
        case 1: {
            granularityFeedback = QString(" at half of standard granularity.");
        } break;
        case 2: {
            granularityFeedback = QString(" at a third of standard granularity.");
        } break;
        default: {
            granularityFeedback = QString(" at 1/%1th of standard granularity.").arg(boundaryLevelAdjust + 1);
        } break;
    }
    
    // distance feedback
    float octreeSizeScale = getOctreeSizeScale();
    float relativeToDefault = octreeSizeScale / DEFAULT_OCTREE_SIZE_SCALE;
    int relativeToTwentyTwenty = 20 / relativeToDefault;

    QString result;
    if (relativeToDefault > 1.01f) {
        result = QString("20:%1 or %2 times further than average vision%3").arg(relativeToTwentyTwenty).arg(relativeToDefault,0,'f',2).arg(granularityFeedback);
    } else if (relativeToDefault > 0.99f) {
        result = QString("20:20 or the default distance for average vision%1").arg(granularityFeedback);
    } else if (relativeToDefault > 0.01f) {
        result = QString("20:%1 or %2 of default distance for average vision%3").arg(relativeToTwentyTwenty).arg(relativeToDefault,0,'f',3).arg(granularityFeedback);
    } else {
        result = QString("%2 of default distance for average vision%3").arg(relativeToDefault,0,'f',3).arg(granularityFeedback);
    }
    return result;
}

static float renderDistance = (float)TREE_SCALE;
static int renderedCount = 0;
static int lastRenderedCount = 0;
bool LODManager::getUseAcuity() { return lodPreference.get() == (int)LODManager::LODPreference::acuity; }
void LODManager::setUseAcuity(bool newValue) { lodPreference.set(newValue ? (int)LODManager::LODPreference::acuity : (int)LODManager::LODPreference::pid); }
float LODManager::getRenderDistance() {
    return renderDistance;
}
int LODManager::getRenderedCount() {
    return lastRenderedCount;
}
QString LODManager::getLODStatsRenderText() {
    QString label = getUseAcuity() ? "Renderable avatars: " : "Rendered objects: ";
    return label + QString::number(getRenderedCount()) + " w/in " + QString::number((int)getRenderDistance()) + "m";
}
// compare audoAdjustLOD()
void LODManager::updatePIDRenderDistance(float targetFps, float measuredFps, float deltaTime, bool isThrottled) {
    float distance;
    if (!isThrottled) {
        _renderDistanceController.setMeasuredValueSetpoint(targetFps / 2.0f); // No problem updating in flight.
        // The PID controller raises the controlled value when the measured value goes up.
        // The measured value is frame rate. When the controlled value (1 / render cutoff distance)
        // goes up, the render cutoff distance gets closer, the number of rendered avatars is less, and frame rate
        // goes up.
        distance = 1.0f / _renderDistanceController.update(measuredFps, deltaTime);
    }
    else {
        // Here we choose to just use the maximum render cutoff distance if throttled.
        distance = 1.0f / _renderDistanceController.getControlledValueLowLimit();
    }
    _renderDistanceAverage.updateAverage(distance);
    renderDistance = _renderDistanceAverage.getAverage(); // average only once per cycle
    lastRenderedCount = renderedCount;
    renderedCount = 0;
}

bool LODManager::shouldRender(const RenderArgs* args, const AABox& bounds) {
    float distanceToCamera = glm::length(bounds.calcCenter() - args->_viewFrustum->getPosition());
    float largestDimension = bounds.getLargestDimension();
    if (!getUseAcuity()) {
        bool isRendered = fabsf(distanceToCamera - largestDimension) < renderDistance;
        renderedCount += isRendered ? 1 : 0;
        return isRendered;
    }

    const float maxScale = (float)TREE_SCALE;
    const float octreeToMeshRatio = 4.0f; // must be this many times closer to a mesh than a voxel to see it.
    float octreeSizeScale = args->_sizeScale;
    int boundaryLevelAdjust = args->_boundaryLevelAdjust;
    float visibleDistanceAtMaxScale = boundaryDistanceForRenderLevel(boundaryLevelAdjust, octreeSizeScale) / octreeToMeshRatio;

    static bool shouldRenderTableNeedsBuilding = true;
    static QMap<float, float> shouldRenderTable;
    if (shouldRenderTableNeedsBuilding) {
        float SMALLEST_SCALE_IN_TABLE = 0.001f; // 1mm is plenty small
        float scale = maxScale;
        float factor = 1.0f;
        
        while (scale > SMALLEST_SCALE_IN_TABLE) {
            scale /= 2.0f;
            factor /= 2.0f;
            shouldRenderTable[scale] = factor;
        }
        
        shouldRenderTableNeedsBuilding = false;
    }
    
    float closestScale = maxScale;
    float visibleDistanceAtClosestScale = visibleDistanceAtMaxScale;
    QMap<float, float>::const_iterator lowerBound = shouldRenderTable.lowerBound(largestDimension);
    if (lowerBound != shouldRenderTable.constEnd()) {
        closestScale = lowerBound.key();
        visibleDistanceAtClosestScale = visibleDistanceAtMaxScale * lowerBound.value();
    }
    
    if (closestScale < largestDimension) {
        visibleDistanceAtClosestScale *= 2.0f;
    }

    return distanceToCamera <= visibleDistanceAtClosestScale;
};

// TODO: This is essentially the same logic used to render octree cells, but since models are more detailed then octree cells
//       I've added a voxelToModelRatio that adjusts how much closer to a model you have to be to see it.
bool LODManager::shouldRenderMesh(float largestDimension, float distanceToCamera) {
    const float octreeToMeshRatio = 4.0f; // must be this many times closer to a mesh than a voxel to see it.
    float octreeSizeScale = getOctreeSizeScale();
    int boundaryLevelAdjust = getBoundaryLevelAdjust();
    float maxScale = (float)TREE_SCALE;
    float visibleDistanceAtMaxScale = boundaryDistanceForRenderLevel(boundaryLevelAdjust, octreeSizeScale) / octreeToMeshRatio;
    
    if (_shouldRenderTableNeedsRebuilding) {
        _shouldRenderTable.clear();
        
        float SMALLEST_SCALE_IN_TABLE = 0.001f; // 1mm is plenty small
        float scale = maxScale;
        float visibleDistanceAtScale = visibleDistanceAtMaxScale;
        
        while (scale > SMALLEST_SCALE_IN_TABLE) {
            scale /= 2.0f;
            visibleDistanceAtScale /= 2.0f;
            _shouldRenderTable[scale] = visibleDistanceAtScale;
        }
        _shouldRenderTableNeedsRebuilding = false;
    }
    
    float closestScale = maxScale;
    float visibleDistanceAtClosestScale = visibleDistanceAtMaxScale;
    QMap<float, float>::const_iterator lowerBound = _shouldRenderTable.lowerBound(largestDimension);
    if (lowerBound != _shouldRenderTable.constEnd()) {
        closestScale = lowerBound.key();
        visibleDistanceAtClosestScale = lowerBound.value();
    }
    
    if (closestScale < largestDimension) {
        visibleDistanceAtClosestScale *= 2.0f;
    }
    
    return (distanceToCamera <= visibleDistanceAtClosestScale);
}

void LODManager::setOctreeSizeScale(float sizeScale) {
    _octreeSizeScale = sizeScale;
    calculateAvatarLODDistanceMultiplier();
    _shouldRenderTableNeedsRebuilding = true;
}

void LODManager::calculateAvatarLODDistanceMultiplier() {
    _avatarLODDistanceMultiplier = AVATAR_TO_ENTITY_RATIO / (_octreeSizeScale / DEFAULT_OCTREE_SIZE_SCALE);
}

void LODManager::setBoundaryLevelAdjust(int boundaryLevelAdjust) {
    _boundaryLevelAdjust = boundaryLevelAdjust;
    _shouldRenderTableNeedsRebuilding = true;
}


void LODManager::loadSettings() {
    setDesktopLODDecreaseFPS(desktopLODDecreaseFPS.get());
    setHMDLODDecreaseFPS(hmdLODDecreaseFPS.get());

    if (lodPreference.get() == (int)LODManager::LODPreference::unspecified) {
        setUseAcuity((getDesktopLODDecreaseFPS() != DEFAULT_DESKTOP_LOD_DOWN_FPS) || (getHMDLODDecreaseFPS() != DEFAULT_HMD_LOD_DOWN_FPS));
    }
    Menu::getInstance()->getActionForOption(MenuOption::LodTools)->setEnabled(getUseAcuity());
    Menu::getInstance()->getSubMenuFromName(MenuOption::RenderResolution, Menu::getInstance()->getSubMenuFromName("Render", Menu::getInstance()->getMenu("Developer")))->setEnabled(getUseAcuity());
}

void LODManager::saveSettings() {
    desktopLODDecreaseFPS.set(getDesktopLODDecreaseFPS());
    hmdLODDecreaseFPS.set(getHMDLODDecreaseFPS());
}


