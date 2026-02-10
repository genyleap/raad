# Packaging Notes

Browser integration will be reintroduced later.

## Update Manifest

The updater can read a JSON manifest from your official website. It accepts either a
single release object or a `releases` array (with `channel` selection).

```json
{
  "version": "1.2.3",
  "notes": "Bug fixes and performance improvements.",
  "assets": [
    { "name": "raad-1.2.3-macos.dmg", "url": "https://example.com/raad-1.2.3-macos.dmg", "platform": "macos", "arch": "arm64" },
    { "name": "raad-1.2.3-win-x64.exe", "url": "https://example.com/raad-1.2.3-win-x64.exe", "platform": "windows", "arch": "x64" },
    { "name": "raad-1.2.3-linux.AppImage", "url": "https://example.com/raad-1.2.3-linux.AppImage", "platform": "linux", "arch": "x64" }
  ]
}
```

```json
{
  "releases": [
    { "channel": "stable", "version": "1.2.3", "notes": "Stable release", "assets": [ { "name": "raad-1.2.3-win-x64.exe", "url": "https://example.com/raad-1.2.3-win-x64.exe" } ] },
    { "channel": "beta", "version": "1.3.0-beta.1", "notes": "Beta release", "assets": [ { "name": "raad-1.3.0-beta.1-win-x64.exe", "url": "https://example.com/raad-1.3.0-beta.1-win-x64.exe" } ] }
  ]
}
```
