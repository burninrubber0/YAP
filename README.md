# YAP
A simple bundle extractor and creator for Burnout Paradise.

## Motivation
YAP was made for two reasons:
1. Easier creation of bundles for recovered development builds; specifically, the various PS3 builds found on a 60GB development kit.
2. Experimental use of YAML for storing bundle and resource metadata, as well as resource imports. This is where it gets its name (YAML Packer).

## Supported versions
Only Bundle 2 version 2, the version used in Burnout Paradise, is supported. Although support for the original bundle format would be nice to have, none of the aforementioned builds use it, so it's outside the scope of this initial release.

## Usage
### Extracting a bundle
```
YAP e <input bundle> <output folder>
```

Once extracted, resources will be output as:
* A primary resource portion named `<ID>.dat` or, if split, `<ID>_primary.dat`
* If split, a secondary resource portion named `<ID>_secondary.dat`
* If imports are present, a file named `<ID>_imports.yaml`

If `--nosort` was used, everything will be output directly into the specified folder. Otherwise, resources will be sorted into subdirectories based on their type.
If `--combine-imports` was used, the imports for every resource will be in `.imports.yaml`.

It may be prudent to apply [this registry edit](https://superuser.com/a/1765437) so files are sorted as expected.

### Creating a bundle
```
YAP c <input folder> <output bundle>
```

This uses the metadata file `.meta.yaml` to construct a bundle. Resources not defined in the metadata file will not be added to the bundle.

Note that the entire input folder, including all subdirectories, is searched indiscriminately for resources. If two resource files have matching names, regardless of their location, they will be detected as duplicates and the creation process will be aborted.

If `.imports.yaml` exists, it will be used during bundle creation. To use split imports instead (provided they've been created), the combined imports file must be removed or renamed.

### Extracting multiple bundles
```
YAP [-e .ext] -d <mode> <input folder> <output bundle>
```
This will extract all bundles from the input folder to the output folder that have the specified extension, if any. If no extension is specified, it will process all bundles in the input folder. The output folder will be created if it doesn't exist.

### Creating multiple bundles
```
YAP [-e .ext] -d <mode> <input folder> <output bundle>
```
This will create a bundle for each folder in the input folder. If an extension is specified, it will only create the bundles from the folders that end with the specified extension, otherwise all of them will be processed. The output folder will be created if it doesn't exist.

### Examples
#### Extracting multiple bundles
```
.\YAP.exe -d e .\Burnout\VEHICLES\ .\Burnout\Extracted\
```
* Extract all bundles in the VEHICLES folder to the Extracted folder.

```
.\YAP.exe -e .BIN .DAT -d e .\Burnout\VEHICLES\ .\Burnout\Extracted\
```
* Extract all bundles with the .BIN and .DAT extensions in the VEHICLES folder to the Extracted folder.

#### Creating multiple bundles
```
.\YAP.exe -d c .\Burnout\Extracted\ .\Burnout\VEHICLES\
```
* Create a bundle for each folder in the Extracted folder and place them in the VEHICLES folder.

```
.\YAP.exe -e .BIN .DAT -d c .\Burnout\Extracted\ .\Burnout\VEHICLES\
```
* Create a bundle for each folder in the Extracted folder that ends with .BIN or .DAT and place them in the VEHICLES folder.

<details>
<summary>⚠️ Important information about folder structure ⚠️</summary>
 The subfolders need to be named with the name of the original bundle and the structure needs to be like this:

  ```yaml
  Burnout
  ├── Extracted
  │   ├── VEHICLES
  │   │   ├── VEH_PUSMC02_BIN --> The name of the original bundle was VEH_PUSMC02.BIN
  │   │   │   ├── .meta.yaml
  │   │   │   ├── ...
  ```
  So it is recommended to only use this on folders where you extracted a bundle using the `-d e` argument to avoid issues.
</details>

#### Important notes about the -e argument
1. You can specify the extension with or without the dot (both `BIN` and `.BIN` work for example)
2. You can specify multiple extensions like: `-e BIN DAT` or `-e .BIN .DAT` or `-e BIN .DAT` or `-e .BIN DAT`
3. If used, this cannot be the last optional argument (that's why it goes before the mode `-d` which is optional before the requiered `c` or `e`)
4. The extension matching is case-insensitive

### Editing bundles
#### Editing imports
Imports look like this:
```yaml
- 0x000000d0: 0xa2abbeb0
- 0x000000e0: 0x6bf39f66
- 0x000000e4: 0x1ceed367
# ...
```

The second value (after the colon) is the ID of the resource to be pointed to. The first value is the offset in the resource that the pointer to the imported resource will be written to at runtime. Imports can be edited, added, or removed at will as there are no checks on them—just make sure they're what the game expects, and remember that different builds expect different imports.

#### Adding resources
There won't typically be a reason to edit the metadata file `.meta.yaml` except to add new resources. Resources look like this:
```yaml
0x00b4802c:
  type: 0xc
  secondaryMemoryType: 1
  alignment:
    - 0x10
    - 0x10
0x00d86b41:
  type: 0xc
  secondaryMemoryType: 1
  alignment:
    - 0x10
    - 0x10
0x019c5442:
  type: 0x1
  alignment:
    - 0x10
# ...
```

To add resources, simply follow the same pattern as other resources, making sure to specify the secondary memory type if the resource is split. Alignment may be excluded, but it is recommended you specify the same value as other resources of the same type. New resources may be added anywhere in the list since it gets sorted automatically.

#### Editing resource data
Resources are made up of binary data and must be created/edited manually. See the [wiki page](https://burnout.wiki/wiki/Resource_Types) for more information.

## Building
Clone the repository:
```
git clone --recurse-submodules https://github.com/burninrubber0/YAP
```

Run CMake:
```
mkdir build
cd build
cmake ..
cmake --build .
```

Building has only been tested on Windows with VS 2022, but in theory nothing is stopping it from being built on other platforms. (Some minor adjustments may be warranted.)

This also requires Qt 6 to be installed with the appropriate environment variables set. Only Qt 6.6.3 has been tested.

## Todo
In no particular order:
* Support other bundle variants
* Allow use of resource names in addition to IDs
* Implement duplicate checking for `.meta.yaml` and `.imports.yaml`, preferably in a way that doesn't tank performance

If you have a feature request, feel free to open an issue describing it.
