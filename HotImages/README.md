# HotImages plugin

See the repository root [README](../README.md) for full documentation. Plugin layout:

```
HotImages/
  HotImages.uplugin
  Config/DefaultGame.ini        # registers Content/HotImages as a staged loose (non-pak) folder
  Source/HotImagesRuntime/      # settings (WatchFolders), subsystem, Windows dir watcher, decode/update
  Source/HotImagesEditor/       # auto import/re-import + reverse png export + staging registration
```

Key behaviors:

- Forward sync: png/jpg dropped or replaced in a WatchFolder auto-creates/refreshes a same-name
  proxy `UTexture2D` (`/Game/<Folder>/<name>`).
- Reverse sync: textures imported/re-imported or edited inside a WatchFolder auto-export a matching
  png back into that folder.
- Packaging: adding a WatchFolder also registers it under "Additional Non-Asset Directories To Copy",
  so the folder is staged as loose files at runtime.
- Runtime: packaged Windows builds watch the loose folder and refresh loaded textures in place.

Engine compatibility: written against UE 5.x APIs (compiled and verified on 5.4 and 5.7).
