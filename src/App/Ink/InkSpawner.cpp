#include "InkSpawner.hpp"

namespace
{
constexpr auto ControllerSeparator = ':';

constexpr auto WaitTimeout = std::chrono::milliseconds(200);
constexpr auto WaitTick = std::chrono::milliseconds(2);
}

void App::InkSpawner::OnBootstrap()
{
    HookOnceAfter<Raw::CBaseEngine::InitEngine>(+[]() {
        if (Red::GetType<"ArchiveXL">())
            return;

        if (!Hook<Raw::InkWidgetLibrary::SpawnFromLocal>(&OnSpawnLocal))
            throw std::runtime_error("Failed to hook InkWidgetLibrary::SpawnFromLocal.");

        if (!Hook<Raw::InkWidgetLibrary::SpawnFromExternal>(&OnSpawnExternal))
            throw std::runtime_error("Failed to hook InkWidgetLibrary::SpawnFromExternal.");

        if (!Hook<Raw::InkWidgetLibrary::AsyncSpawnFromLocal>(&OnAsyncSpawnLocal))
            throw std::runtime_error("Failed to hook InkWidgetLibrary::AsyncSpawnFromLocal.");

        if (!Hook<Raw::InkWidgetLibrary::AsyncSpawnFromExternal>(&OnAsyncSpawnExternal))
            throw std::runtime_error("Failed to hook InkWidgetLibrary::AsyncSpawnFromExternal.");

        if (!HookBefore<Raw::InkSpawner::FinishAsyncSpawn>(&OnFinishAsyncSpawn))
            throw std::runtime_error("Failed to hook InkSpawner::FinishAsyncSpawn.");
    });
}

void App::InkSpawner::OnShutdown()
{
    Unhook<Raw::InkWidgetLibrary::SpawnFromLocal>();
    Unhook<Raw::InkWidgetLibrary::SpawnFromExternal>();
    Unhook<Raw::InkWidgetLibrary::AsyncSpawnFromLocal>();
    Unhook<Raw::InkWidgetLibrary::AsyncSpawnFromExternal>();
    Unhook<Raw::InkSpawner::FinishAsyncSpawn>();
}

uintptr_t App::InkSpawner::OnSpawnLocal(Red::ink::WidgetLibraryResource& aLibrary,
                                        Red::Handle<Red::ink::WidgetLibraryItemInstance>& aInstance,
                                        Red::CName aItemName)
{
    auto result = Raw::InkWidgetLibrary::SpawnFromLocal(aLibrary, aInstance, aItemName);

    if (!aInstance)
    {
        auto* itemNameStr = aItemName.ToString();
        auto* controllerSep = strchr(itemNameStr, ControllerSeparator);

        if (controllerSep)
        {
            Red::CName itemName(Red::FNV1a64(reinterpret_cast<const uint8_t*>(itemNameStr), controllerSep - itemNameStr));
            Raw::InkWidgetLibrary::SpawnFromLocal(aLibrary, aInstance, itemName);

            if (aInstance)
            {
                InjectController(aInstance, controllerSep + 1);
            }
        }
    }

    return result;
}

uintptr_t App::InkSpawner::OnSpawnExternal(Red::ink::WidgetLibraryResource& aLibrary,
                                           Red::Handle<Red::ink::WidgetLibraryItemInstance>& aInstance,
                                           Red::ResourcePath aExternalPath,
                                           Red::CName aItemName)
{
    InjectDependency(aLibrary, aExternalPath);

    auto result = Raw::InkWidgetLibrary::SpawnFromExternal(aLibrary, aInstance, aExternalPath, aItemName);

    if (!aInstance)
    {
        auto* itemNameStr = aItemName.ToString();
        auto* controllerSep = strchr(itemNameStr, ControllerSeparator);

        if (controllerSep)
        {
            Red::CName itemName(Red::FNV1a64(reinterpret_cast<const uint8_t*>(itemNameStr), controllerSep - itemNameStr));
            Raw::InkWidgetLibrary::SpawnFromExternal(aLibrary, aInstance, aExternalPath, itemName);

            if (aInstance)
            {
                InjectController(aInstance, controllerSep + 1);
            }
        }
    }

    return result;
}

bool App::InkSpawner::OnAsyncSpawnLocal(Red::ink::WidgetLibraryResource& aLibrary,
                                        Red::InkSpawningInfo& aSpawningInfo,
                                        Red::CName aItemName,
                                        uint8_t aParam)
{
    auto* itemNameStr = aItemName.ToString();
    auto* controllerSep = strchr(itemNameStr, ControllerSeparator);

    if (controllerSep)
    {
        aItemName = Red::FNV1a64(reinterpret_cast<const uint8_t*>(itemNameStr), controllerSep - itemNameStr);
    }

    return Raw::InkWidgetLibrary::AsyncSpawnFromLocal(aLibrary, aSpawningInfo, aItemName, aParam);
}

bool App::InkSpawner::OnAsyncSpawnExternal(Red::ink::WidgetLibraryResource& aLibrary,
                                           Red::InkSpawningInfo& aSpawningInfo,
                                           Red::ResourcePath aExternalPath,
                                           Red::CName aItemName,
                                           uint8_t aParam)
{
    InjectDependency(aLibrary, aExternalPath);

    auto* itemNameStr = aItemName.ToString();
    auto* controllerSep = strchr(itemNameStr, ControllerSeparator);

    if (controllerSep)
    {
        aItemName = Red::FNV1a64(reinterpret_cast<const uint8_t*>(itemNameStr), controllerSep - itemNameStr);
    }

    return Raw::InkWidgetLibrary::AsyncSpawnFromExternal(aLibrary, aSpawningInfo, aExternalPath, aItemName, aParam);
}

void App::InkSpawner::OnFinishAsyncSpawn(Red::InkSpawningContext& aContext,
                                         Red::Handle<Red::ink::WidgetLibraryItemInstance>& aInstance)
{
    auto* itemNameStr = aContext.request->itemName.ToString();
    auto* controllerSep = strchr(itemNameStr, ControllerSeparator);

    if (controllerSep)
    {
        InjectController(aInstance, controllerSep + 1);
    }
}

void App::InkSpawner::InjectDependency(Red::ink::WidgetLibraryResource& aLibrary, Red::ResourcePath aExternalPath)
{
    bool libraryExists = false;

    // Check if the external library is in the list and do nothing if it is
    {
        std::shared_lock _(s_mutex);
        for (const auto& externalLibrary : aLibrary.externalLibraries)
        {
            if (externalLibrary.path == aExternalPath)
            {
                libraryExists = true;
                break;
            }
        }
    }

    // Add the requested library to the list
    if (!libraryExists)
    {
        std::unique_lock _(s_mutex);
        aLibrary.externalLibraries.EmplaceBack(aExternalPath);

        // Load requested library for the spawner
        auto* externalLibrary = aLibrary.externalLibraries.End() - 1;
        externalLibrary->LoadAsync();

        const auto start = std::chrono::steady_clock::now();
        while (!externalLibrary->IsLoaded() && !externalLibrary->IsFailed())
        {
            std::this_thread::sleep_for(WaitTick);

            if (std::chrono::steady_clock::now() - start >= WaitTimeout)
                break;
        }
    }
}

void App::InkSpawner::InjectController(Red::Handle<Red::ink::WidgetLibraryItemInstance>& aInstance,
                                       Red::CName aControllerName)
{
    static const Red::ClassLocator<Red::ink::IWidgetController> s_gameControllerType;
    static const Red::ClassLocator<Red::ink::WidgetLogicController> s_logicControllerType;

    auto* controllerType = Red::CRTTISystem::Get()->GetClass(aControllerName);

    if (controllerType)
    {
        if (controllerType->IsA(s_gameControllerType))
        {
            auto* controllerInstance = reinterpret_cast<Red::ink::IWidgetController*>(controllerType->CreateInstance(true));
            Red::Handle<Red::ink::IWidgetController> controllerHandle(controllerInstance);

            aInstance->gameController.Swap(controllerHandle);

            if (controllerHandle.instance)
            {
                InheritProperties(controllerInstance, controllerHandle.instance);
            }
        }
        else if (controllerType->IsA(s_logicControllerType))
        {
            auto* controllerInstance = reinterpret_cast<Red::ink::WidgetLogicController*>(controllerType->CreateInstance(true));
            Red::Handle<Red::ink::WidgetLogicController> controllerHandle(controllerInstance);

            aInstance->rootWidget->logicController.Swap(controllerHandle);

            if (controllerHandle.instance)
            {
                InheritProperties(controllerInstance, controllerHandle.instance);
            }
        }
    }
}

void App::InkSpawner::InheritProperties(Red::IScriptable* aTarget, Red::IScriptable* aSource)
{
    auto* sourceType = aSource->GetType();
    auto* targetType = aTarget->GetType();

    Red::DynArray<Red::CProperty*> sourceProps;
    sourceType->GetProperties(sourceProps);

    for (const auto& sourceProp : sourceProps)
    {
        const auto targetProp = targetType->GetProperty(sourceProp->name);

        if (targetProp && targetProp->type == sourceProp->type)
        {
            targetProp->SetValue(aTarget, sourceProp->GetValuePtr<void>(aSource));
        }
    }
}
