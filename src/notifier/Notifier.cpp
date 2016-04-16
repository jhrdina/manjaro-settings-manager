/*
 *  Manjaro Settings Manager
 *  Roland Singer <roland@manjaro.org>
 *  Ramon Buldó <ramon@manjaro.org>
 *
 *  Copyright (C) 2007 Free Software Foundation, Inc.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "LanguageCommon.h"
#include "LanguagePackages.h"
#include "Notifier.h"
#include "NotifierApp.h"
#include "Kernel.h"
#include "KernelModel.h"

#include <QtWidgets/QAction>
#include <QtWidgets/QMenu>
#include <QtCore/QFile>
#include <QtCore/QProcess>
#include <QtCore/QSettings>

#include <QDebug>

Notifier::Notifier( QObject* parent ) :
    QObject( parent )
{
    m_tray = new KStatusNotifierItem( this );
    m_tray->setTitle( QString( tr ( "Manjaro Settings Manager" ) ) );
    m_tray->setIconByName( "manjaro-settings-manager" );
    m_tray->setStatus( KStatusNotifierItem::Passive );

    auto menu = m_tray->contextMenu();

    QAction* msmKernel = new QAction( QIcon( ":/images/resources/tux-manjaro.png" ),
                                      QString( tr ( "Kernels" ) ),
                                      menu );
    QAction* msmLanguagePackages = new QAction(
        QIcon( ":/images/resources/language.png" ),
        QString( tr ( "Language packages" ) ),
        menu );
    menu->addAction( msmKernel );
    menu->addAction( msmLanguagePackages );

    connect( msmKernel, &QAction::triggered, this, [msmKernel, this]()
    {
        QProcess::startDetached( "msm", QStringList() << "-m" << "msm_kernel" );
        m_tray->setStatus( KStatusNotifierItem::Passive );
    } );
    connect( msmLanguagePackages, &QAction::triggered, this, [msmLanguagePackages, this]()
    {
        QProcess::startDetached( "msm", QStringList() << "-m" << "msm_language_packages" );
        m_tray->setStatus( KStatusNotifierItem::Passive );
    } );

    m_timer = new QTimer( this );
    // 1 min
    m_timer->setInterval( 60 * 1000 );
    m_timer->start();

    connect( m_timer, &QTimer::timeout, [=] ()
    {
        loadConfiguration();
        if ( !isPacmanUpdating() && hasPacmanEverSynced() )
        {
            if ( m_checkLanguagePackage )
                cLanguagePackage();

            if ( m_checkKernel )
                cKernel();

            // 12 hours
            m_timer->setInterval( 12 * 60 * 60 * 1000 );
        }
        else
        {
            // 30 minutes
            m_timer->setInterval( 30 * 60 * 1000 );
        }
    } );
}


Notifier::~Notifier()
{

}


void
Notifier::cLanguagePackage()
{
    int packageNumber = 0;
    QStringList packages;
    LanguagePackages languagePackages;
    QList<LanguagePackagesItem> lpiList { languagePackages.languagePackages() };
    QStringList locales { LanguageCommon::enabledLocales( true ) };
    foreach ( const QString locale, locales )
    {
        QStringList split = locale.split( "_", QString::SkipEmptyParts );
        if ( split.size() != 2 )
            continue;
        QByteArray language = QString( split.at( 0 ) ).toUtf8();
        QByteArray territory = QString( split.at( 1 ) ).toUtf8();

        foreach ( const auto item, lpiList )
        {
            if ( item.parentPkgInstalled().length() == 0 )
                continue;
            if ( !item.languagePackage().contains( "%" ) )
                continue;

            QList<QByteArray> checkPkgs;
            // Example: firefox-i18n-% -> firefox-i18n-en-US
            checkPkgs << item.languagePackage().replace( "%", language.toLower() + "-" + territory );
            // Example: firefox-i18n-% -> firefox-i18n-en-us
            checkPkgs << item.languagePackage().replace( "%", language.toLower() + "-" + territory.toLower() );
            // Example: firefox-i18n-% -> firefox-i18n-en_US
            checkPkgs << item.languagePackage().replace( "%", language.toLower() + "_" + territory );
            // Example: firefox-i18n-% -> firefox-i18n-en_us
            checkPkgs << item.languagePackage().replace( "%", language.toLower() + "_" + territory.toLower() );
            // Example: firefox-i18n-% -> firefox-i18n-en
            checkPkgs << item.languagePackage().replace( "%", language.toLower() );

            foreach ( const auto checkPkg, checkPkgs )
            {
                if ( item.languagePkgInstalled().contains( checkPkg ) )
                    continue;
                if ( item.languagePkgAvailable().contains( checkPkg ) )
                {
                    ++packageNumber;
                    if ( !isPackageIgnored( checkPkg, "language_package" ) )
                        packages << checkPkg;
                }
            }
        }
    }
    foreach ( const auto item, lpiList )
    {
        if ( item.parentPkgInstalled().length() == 0 )
            continue;
        if ( !item.languagePackage().contains( "%" )
                && !item.languagePkgInstalled().contains( item.languagePackage() ) )
        {
            qDebug() << item.languagePackage();
            ++packageNumber;
            if ( !isPackageIgnored( item.languagePackage(), "language_package" ) )
                packages << item.languagePackage();
        }
    }

    if ( !packages.isEmpty() )
    {
        qDebug() << "Missing language packages found, notifying user...";
        m_tray->setStatus( KStatusNotifierItem::Active );
        m_tray->showMessage( tr( "Manjaro Settings Manager" ),
                             QString( tr( "%n new additional language package(s) available", "", packageNumber ) ),
                             QString( "dialog-information" ),
                             10000 );

        // Add to Config
        foreach ( const QString package, packages )
            addToConfig( package, "language_package" );
    }
}


void
Notifier::cKernel()
{
    Notifier::KernelFlags kernelFlags;
    KernelModel kernelModel;
    kernelModel.update();

    QList< Kernel > unsupportedKernels = kernelModel.unsupportedKernels();
    if ( m_checkUnsupportedKernel && !unsupportedKernels.isEmpty() )
    {
        foreach ( Kernel kernel, unsupportedKernels )
        {
            if ( isPackageIgnored( kernel.package(), "unsupported_kernel" ) )
            {
                qDebug() << "Found ignored unsupported kernel: " << kernel.version();
                continue;
            }

            kernelFlags |= KernelFlag::Unsupported;
            if ( m_checkUnsupportedKernelRunning && kernel.isRunning() )
            {
                kernelFlags |= KernelFlag::Running;
                qDebug() << "Found unsupported kernel running: " << kernel.version();
            }
            else
                qDebug() << "Found unsupported kernel: " << kernel.version();
        }
    }

    qDebug() << "Latest installed kernel: " << kernelModel.latestInstalledKernel().version();
    QList<Kernel> newKernels = kernelModel.newerKernels( kernelModel.latestInstalledKernel() );
    QList<Kernel> newLtsRecommendedKernels;
    QList<Kernel> newLtsKernels;
    QList<Kernel> newRecommendedKernels;
    QList<Kernel> newNotIgnoredKernels;
    if ( m_checkNewKernel )
    {
        foreach ( Kernel kernel, newKernels )
        {
            if ( isPackageIgnored( kernel.package(), "new_kernel" ) )
            {
                qDebug() << "Found newer kernel, but ignored: " << kernel.version();
                continue;
            }
            newNotIgnoredKernels << kernel;
            if ( kernel.isRecommended() && kernel.isLts() )
            {
                qDebug() << "Found newer kernel LTS and recommended: " << kernel.version();
                newLtsRecommendedKernels << kernel;
                newLtsKernels << kernel;
                newRecommendedKernels << kernel;
            }
            else if ( kernel.isLts() )
            {
                qDebug() << "Found newer kernel LTS: " << kernel.version();
                newLtsKernels << kernel;
            }
            else if ( kernel.isRecommended() )
            {
                qDebug() << "Found newer kernel recommended: " << kernel.version();
                newRecommendedKernels << kernel;
            }
            else
                qDebug() << "Found newer kernel: " << kernel.version();
        }


        if ( m_checkNewKernelLts && m_checkNewKernelRecommended )
        {
            if ( !newLtsRecommendedKernels.isEmpty() )
                kernelFlags |= KernelFlag::New;
        }
        else if ( m_checkNewKernelLts )
        {
            if ( !newLtsKernels.isEmpty() )
                kernelFlags |= KernelFlag::New;
        }
        else if ( m_checkNewKernelRecommended )
        {
            if ( !newRecommendedKernels.isEmpty() )
                kernelFlags |= KernelFlag::New;
        }
        else
        {
            if ( !newNotIgnoredKernels.isEmpty() )
                kernelFlags |= KernelFlag::New;
        }
    }

    /*
    kernelFlags = 0;
    kernelFlags |= KernelFlag::Unsupported;
    kernelFlags |= KernelFlag::Running;
    kernelFlags |= KernelFlag::New;
    */

    if  ( kernelFlags.testFlag( KernelFlag::Unsupported ) || kernelFlags.testFlag( KernelFlag::New ) )
        m_tray->setStatus( KStatusNotifierItem::Active );

    // Notify about unsupported kernels
    if ( kernelFlags.testFlag( KernelFlag::Unsupported ) )
    {
        QString messageTitle = QString( tr( "Manjaro Settings Manager" ) );
        if ( kernelFlags.testFlag( KernelFlag::Running ) )
        {
            m_tray->showMessage( messageTitle,
                                 QString( tr( "Running an unsupported kernel, please update." ) ),
                                 QString( "dialog-warning" ),
                                 10000 );
        }
        else
        {
            m_tray->showMessage( messageTitle,
                                 QString( tr( "Unsupported kernel installed in your system, please remove it." ) ),
                                 QString( "dialog-information" ),
                                 10000 );
        }
        foreach ( Kernel kernel, unsupportedKernels )
            addToConfig( kernel.package(), "unsupported_kernel" );
    }

    // Notify about new kernels
    if ( kernelFlags.testFlag( KernelFlag::New ) )
    {
        m_tray->showMessage( QString( tr( "Manjaro Settings Manager" ) ),
                             QString( tr( "Newer kernel is available, please update." ) ),
                             QString( "dialog-information" ),
                             10000 );
        foreach ( Kernel kernel, newNotIgnoredKernels )
            addToConfig( kernel.package(), "new_kernel" );
    }
}

void
Notifier::loadConfiguration()
{
    QSettings settings( "manjaro", "manjaro-settings-manager" );
    m_checkLanguagePackage = settings.value( "notifications/checkLanguagePackages", true ).toBool();
    m_checkUnsupportedKernel = settings.value( "notifications/checkUnsupportedKernel", true ).toBool();
    m_checkUnsupportedKernelRunning = settings.value( "notifications/checkUnsupportedKernelRunning", false ).toBool();
    m_checkNewKernel = settings.value( "notifications/checkNewKernel", true ).toBool();
    m_checkNewKernelLts = settings.value( "notifications/checkNewKernelLts", false ).toBool();
    m_checkNewKernelRecommended = settings.value( "notifications/checkNewKernelRecommended", true ).toBool();
    m_checkKernel = m_checkUnsupportedKernel | m_checkNewKernel;
}


bool
Notifier::isPackageIgnored( const QString package, const QString group )
{
    QSettings settings( "manjaro", "manjaro-settings-manager-Notifier" );
    settings.beginGroup( group );
    int value = settings.value( "notify_count_" + package, "0" ).toInt();
    settings.endGroup();
    return ( value < 2 ) ? false : true;
}


void
Notifier::addToConfig( const QString package, const QString group )
{
    QSettings settings( "manjaro", "manjaro-settings-manager-Notifier" );
    settings.beginGroup( group );
    int value = settings.value( "notify_count_" + package, "0" ).toInt();
    ++value;
    if ( value < 3 )
        settings.setValue( "notify_count_" + package, value );
    settings.endGroup();
}


bool
Notifier::hasPacmanEverSynced()
{
    QString path( "/var/lib/pacman/sync/" );
    QStringList files = QStringList() << "core.db" << "community.db" << "extra.db";
    foreach ( QString f, files )
    {
        if ( !QFile::exists( path + f ) )
            return false;
    }
    return true;
}


bool
Notifier::isPacmanUpdating()
{
    return QFile::exists( "/var/lib/pacman/db.lck" );
}
