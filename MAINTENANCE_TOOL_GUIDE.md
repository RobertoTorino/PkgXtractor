# Qt Installer Framework Maintenance Tool Guide

This guide explains how to use the Qt Installer Framework (QtIFW) Maintenance Tool to enable in-app updates for your application.

---

## 1. Install Qt Installer Framework
- Download from: https://download.qt.io/official_releases/qt-installer-framework/
- Install or extract the tools (look for `binarycreator.exe`, `repogen.exe`, etc.).

---

## 2. Create Your Installer Project Structure
Organize a directory like this:
```
installer/
  config/
    config.xml
  packages/
    com.yourcompany.pkgxtractor/
      meta/
        package.xml
        installscript.qs   # (optional, for custom install logic)
      data/
        (your app files: exe, DLLs, etc.)
```

---

## 3. Write `config.xml`
Example:
```xml
<Installer>
    <Name>PkgXtractor</Name>
    <Version>1.0.0</Version>
    <Title>PkgXtractor Installer</Title>
    <Publisher>Your Company</Publisher>
    <StartMenuDir>PkgXtractor</StartMenuDir>
    <TargetDir>@RootDir@/PkgXtractor</TargetDir>
    <MaintenanceToolName>maintenancetool.exe</MaintenanceToolName>
    <RemoteRepositories>
        <Repository>
            <Url>https://yourdomain.com/qtifw-repo/</Url>
        </Repository>
    </RemoteRepositories>
</Installer>
```
- The `<RemoteRepositories>` section enables online updates.

---

## 4. Write `package.xml` for Your App
Example:
```xml
<Package>
    <DisplayName>PkgXtractor</DisplayName>
    <Description>PS4 PKG extraction tool</Description>
    <Version>1.0.0</Version>
    <ReleaseDate>2026-03-21</ReleaseDate>
    <Default>true</Default>
    <Script>installscript.qs</Script> <!-- optional -->
</Package>
```

---

## 5. Build the Installer
From the `installer/` directory, run:
```sh
binarycreator --config config/config.xml --packages packages pkgxtractor-installer.exe
```
- This creates `pkgxtractor-installer.exe` with the Maintenance Tool included.

---

## 6. Host an Online Repository (for Updates)
- Use `repogen` to generate a repository from your `packages` directory:
  ```sh
  repogen --packages packages --repository myrepo
  ```
- Upload the `myrepo` folder to your web server (URL must match the one in `config.xml`).

---

## 7. How Updates Work
- When you release a new version, update the `Version` in `package.xml` and rebuild the installer and repository.
- Users can run `maintenancetool.exe` (installed with your app) to check for and install updates.
- You can also launch the Maintenance Tool from your app (e.g., via a menu or update button).

---

## 8. Launching the Maintenance Tool from Your App
In C++/Qt:
```cpp
QProcess::startDetached(QCoreApplication::applicationDirPath() + "/maintenancetool.exe");
```
- This opens the Maintenance Tool UI for the user.

---

## 9. Advanced: Silent Updates
- The Maintenance Tool supports command-line options for silent or automatic updates.
- See: https://doc.qt.io/qtinstallerframework/silent.html

---

## 10. Documentation
- Official QtIFW docs: https://doc.qt.io/qtinstallerframework/index.html
- Maintenance Tool: https://doc.qt.io/qtinstallerframework/maintenance-tool.html

---

Let us know if you need a ready-to-use example directory or help with any step!