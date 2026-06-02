#pragma once

#include "asset/AssetManager.hpp"

#include "core/Application.hpp"
#include "input/IInputProvider.hpp"
#ifdef ATLAS_PLATFORM_DESKTOP
#include "../platform/desktop/DesktopInputProvider.hpp"
#endif
#include "core/Layer.hpp"
#include "core/LayerStack.hpp"
#include "core/Log.hpp"
#include "core/Core.hpp"

#include "entity/Object.hpp"

#include "project/ProjectManifest.hpp"
#include "project/ProjectInstance.hpp"
#include "project/ProjectLayer.hpp"
#include "project/ProjectModule.hpp"

#include "renderer/Renderer.hpp"

#include "scene/IScene.hpp"
#include "scene/LevelScene.hpp"
#include "scene/LevelSerializer.hpp"

#include "system/CameraSystem.hpp"
#include "system/RenderSystemV2.hpp"
