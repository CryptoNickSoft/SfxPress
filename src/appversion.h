/*---------------------------------------------------------------------------*/
/* appversion.h — единственный источник имени и версии приложения.           */
/* Используется и в C++ (main.cpp), и в ресурсах (app.rc -> VERSIONINFO).    */
/* Меняете версию только здесь.                                              */
/*---------------------------------------------------------------------------*/
#ifndef APPVERSION_H
#define APPVERSION_H

#define APP_NAME            "SfxPress"
#define APP_VERSION         "1.1"

#define APP_VERSION_MAJOR   1
#define APP_VERSION_MINOR   1
#define APP_VERSION_PATCH   0
#define APP_VERSION_BUILD   0

#define APP_COMPANY         "CryptoNickSoft"
#define APP_DESCRIPTION     "SfxPress - SFX installer builder"

#ifndef RC_INVOKED
#define APP_NAME_W          L"SfxPress"
#define APP_VERSION_W       L"1.1"
#endif

#endif /* APPVERSION_H */
