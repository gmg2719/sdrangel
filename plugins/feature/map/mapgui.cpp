///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2021 Jon Beniston, M7RCE                                        //
// Copyright (C) 2020 Edouard Griffiths, F4EXB                                   //
//                                                                               //
// This program is free software; you can redistribute it and/or modify          //
// it under the terms of the GNU General Public License as published by          //
// the Free Software Foundation as version 3 of the License, or                  //
// (at your option) any later version.                                           //
//                                                                               //
// This program is distributed in the hope that it will be useful,               //
// but WITHOUT ANY WARRANTY; without even the implied warranty of                //
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                  //
// GNU General Public License V3 for more details.                               //
//                                                                               //
// You should have received a copy of the GNU General Public License             //
// along with this program. If not, see <http://www.gnu.org/licenses/>.          //
///////////////////////////////////////////////////////////////////////////////////

#include <cmath>
#include <QMessageBox>
#include <QLineEdit>
#include <QQmlContext>
#include <QQmlProperty>
#include <QQuickItem>
#include <QGeoLocation>
#include <QGeoCoordinate>
#include <QGeoCodingManager>
#include <QGeoServiceProvider>
#include <QRegExp>

#include "feature/featureuiset.h"
#include "gui/basicfeaturesettingsdialog.h"
#include "channel/channelwebapiutils.h"
#include "mainwindow.h"
#include "device/deviceuiset.h"
#include "util/units.h"
#include "util/maidenhead.h"
#include "maplocationdialog.h"
#include "mapmaidenheaddialog.h"
#include "mapsettingsdialog.h"

#include "ui_mapgui.h"
#include "map.h"
#include "mapgui.h"
#include "SWGMapItem.h"
#include "SWGTargetAzimuthElevation.h"

void MapItem::findFrequency()
{
    // Look for a frequency in the text for this object
    QRegExp re("(([0-9]+(\\.[0-9]+)?) *([kMG])?Hz)");
    if (re.indexIn(m_text) != -1)
    {
        QStringList capture = re.capturedTexts();
        m_frequency = capture[2].toDouble();
        if (capture.length() == 5)
        {
            QChar unit = capture[4][0];
            if (unit == 'k')
                m_frequency *= 1000.0;
            else if (unit == 'M')
                m_frequency *= 1000000.0;
            else if (unit == 'G')
                m_frequency *= 1000000000.0;
        }
        m_frequencyString = capture[0];
    }
    else
        m_frequency = 0.0;
}

QVariant MapModel::data(const QModelIndex &index, int role) const
{
    int row = index.row();

    if ((row < 0) || (row >= m_items.count()))
        return QVariant();
    if (role == MapModel::positionRole)
    {
        // Coordinates to display the item at
        QGeoCoordinate coords;
        coords.setLatitude(m_items[row]->m_latitude);
        coords.setLongitude(m_items[row]->m_longitude);
        return QVariant::fromValue(coords);
    }
    else if (role == MapModel::mapTextRole)
    {
        // Create the text to go in the bubble next to the image
        if (row == m_target)
        {
            AzEl *azEl = m_gui->getAzEl();
            QString text = QString("%1\nAz: %2 El: %3")
                                .arg(m_selected[row] ? m_items[row]->m_text : m_items[row]->m_name)
                                .arg(std::round(azEl->getAzimuth()))
                                .arg(std::round(azEl->getElevation()));
            return QVariant::fromValue(text);
        }
        else if (m_selected[row])
            return QVariant::fromValue(m_items[row]->m_text);
        else
            return QVariant::fromValue(m_items[row]->m_name);
    }
    else if (role == MapModel::mapTextVisibleRole)
    {
        return QVariant::fromValue((m_selected[row] || m_displayNames) && (m_sources & m_items[row]->m_sourceMask));
    }
    else if (role == MapModel::mapImageVisibleRole)
    {
        return QVariant::fromValue((m_sources & m_items[row]->m_sourceMask) != 0);
    }
    else if (role == MapModel::mapImageRole)
    {
        // Set an image to use
        return QVariant::fromValue(m_items[row]->m_image);
    }
    else if (role == MapModel::mapImageRotationRole)
    {
        // Angle to rotate image by
        return QVariant::fromValue(m_items[row]->m_imageRotation);
    }
    else if (role == MapModel::mapImageMinZoomRole)
    {
        // Minimum zoom level
        return QVariant::fromValue(m_items[row]->m_imageMinZoom);
    }
    else if (role == MapModel::bubbleColourRole)
    {
        // Select a background colour for the text bubble next to the item
        if (m_selected[row])
            return QVariant::fromValue(QColor("lightgreen"));
        else
            return QVariant::fromValue(QColor("lightblue"));
    }
    else if (role == MapModel::selectedRole)
        return QVariant::fromValue(m_selected[row]);
    else if (role == MapModel::targetRole)
        return QVariant::fromValue(m_target == row);
    else if (role == MapModel::frequencyRole)
        return QVariant::fromValue(m_items[row]->m_frequency);
    else if (role == MapModel::frequencyStringRole)
        return QVariant::fromValue(m_items[row]->m_frequencyString);
    return QVariant();
}

bool MapModel::setData(const QModelIndex &idx, const QVariant& value, int role)
{
    int row = idx.row();
    if ((row < 0) || (row >= m_items.count()))
        return false;
    if (role == MapModel::selectedRole)
    {
        m_selected[row] = value.toBool();
        emit dataChanged(idx, idx);
        return true;
    }
    else if (role == MapModel::targetRole)
    {
        if (m_target >= 0)
        {
            // Update text bubble for old target
            QModelIndex oldIdx = index(m_target);
            m_target = -1;
            emit dataChanged(oldIdx, oldIdx);
        }
        m_target = row;
        updateTarget();
        emit dataChanged(idx, idx);
        return true;
    }
    return true;
}

void MapModel::setFrequency(double frequency)
{
    // Set as centre frequency
    ChannelWebAPIUtils::setCenterFrequency(0, frequency);
}

void MapModel::update(const PipeEndPoint *sourcePipe, SWGSDRangel::SWGMapItem *swgMapItem, quint32 sourceMask)
{
    QString name = *swgMapItem->getName();
    // Add, update or delete and item
    MapItem *item = findMapItem(sourcePipe, name);
    if (item != nullptr)
    {
        QString image = *swgMapItem->getImage();
        if (image.isEmpty())
        {
            // Delete the item
            remove(item);
        }
        else
        {
            // Update the item
            item->update(swgMapItem);
            update(item);
        }
    }
    else
    {
        // Make sure not a duplicate request to delete
        QString image = *swgMapItem->getImage();
        if (!image.isEmpty())
        {
            if (!sourceMask)
                sourceMask = m_gui->getSourceMask(sourcePipe);
            // Add new item
            add(new MapItem(sourcePipe, sourceMask, swgMapItem));
        }
    }
}

void MapModel::updateTarget()
{
    // Calculate range, azimuth and elevation to object from station
    AzEl *azEl = m_gui->getAzEl();
    azEl->setTarget(m_items[m_target]->m_latitude, m_items[m_target]->m_longitude, m_items[m_target]->m_altitude);
    azEl->calculate();

    // Send to Rotator Controllers
    MessagePipes& messagePipes = MainCore::instance()->getMessagePipes();
    QList<MessageQueue*> *mapMessageQueues = messagePipes.getMessageQueues(m_gui->getMap(), "target");
    if (mapMessageQueues)
    {
        QList<MessageQueue*>::iterator it = mapMessageQueues->begin();

        for (; it != mapMessageQueues->end(); ++it)
        {
            SWGSDRangel::SWGTargetAzimuthElevation *swgTarget = new SWGSDRangel::SWGTargetAzimuthElevation();
            swgTarget->setName(new QString(m_items[m_target]->m_name));
            swgTarget->setAzimuth(azEl->getAzimuth());
            swgTarget->setElevation(azEl->getElevation());
            (*it)->push(MainCore::MsgTargetAzimuthElevation::create(m_gui->getMap(), swgTarget));
        }
    }
}

MapGUI* MapGUI::create(PluginAPI* pluginAPI, FeatureUISet *featureUISet, Feature *feature)
{
    MapGUI* gui = new MapGUI(pluginAPI, featureUISet, feature);
    return gui;
}

void MapGUI::destroy()
{
    delete this;
}

void MapGUI::resetToDefaults()
{
    m_settings.resetToDefaults();
    displaySettings();
    applySettings(true);
}

QByteArray MapGUI::serialize() const
{
    return m_settings.serialize();
}

bool MapGUI::deserialize(const QByteArray& data)
{
    if (m_settings.deserialize(data))
    {
        displaySettings();
        applySettings(true);
        return true;
    }
    else
    {
        resetToDefaults();
        return false;
    }
}

bool MapGUI::handleMessage(const Message& message)
{
    if (Map::MsgConfigureMap::match(message))
    {
        qDebug("MapGUI::handleMessage: Map::MsgConfigureMap");
        const Map::MsgConfigureMap& cfg = (Map::MsgConfigureMap&) message;
        m_settings = cfg.getSettings();
        blockApplySettings(true);
        displaySettings();
        blockApplySettings(false);

        return true;
    }
    else if (PipeEndPoint::MsgReportPipes::match(message))
    {
        PipeEndPoint::MsgReportPipes& report = (PipeEndPoint::MsgReportPipes&) message;
        m_availablePipes = report.getAvailablePipes();

        return true;
    }
    else if (Map::MsgFind::match(message))
    {
        Map::MsgFind& msgFind = (Map::MsgFind&) message;
        find(msgFind.getTarget());
        return true;
    }
    else if (MainCore::MsgMapItem::match(message))
    {
        MainCore::MsgMapItem& msgMapItem = (MainCore::MsgMapItem&) message;
        SWGSDRangel::SWGMapItem *swgMapItem = msgMapItem.getSWGMapItem();
        m_mapModel.update(msgMapItem.getPipeSource(), swgMapItem);
        return true;
    }

    return false;
}

void MapGUI::handleInputMessages()
{
    Message* message;

    while ((message = getInputMessageQueue()->pop()))
    {
        if (handleMessage(*message)) {
            delete message;
        }
    }
}

void MapGUI::onWidgetRolled(QWidget* widget, bool rollDown)
{
    (void) widget;
    (void) rollDown;
}

MapGUI::MapGUI(PluginAPI* pluginAPI, FeatureUISet *featureUISet, Feature *feature, QWidget* parent) :
    FeatureGUI(parent),
    ui(new Ui::MapGUI),
    m_pluginAPI(pluginAPI),
    m_featureUISet(featureUISet),
    m_doApplySettings(true),
    m_mapModel(this),
    m_beacons(nullptr),
    m_beaconDialog(this)
{
    ui->setupUi(this);

    ui->map->rootContext()->setContextProperty("mapModel", &m_mapModel);
    ui->map->setSource(QUrl(QStringLiteral("qrc:/map/map/map.qml")));

    setAttribute(Qt::WA_DeleteOnClose, true);
    setChannelWidget(false);
    connect(this, SIGNAL(widgetRolled(QWidget*,bool)), this, SLOT(onWidgetRolled(QWidget*,bool)));
    m_map = reinterpret_cast<Map*>(feature);
    m_map->setMessageQueueToGUI(&m_inputMessageQueue);

    m_featureUISet->addRollupWidget(this);

    connect(this, SIGNAL(customContextMenuRequested(const QPoint &)), this, SLOT(onMenuDialogCalled(const QPoint &)));
    connect(getInputMessageQueue(), SIGNAL(messageEnqueued()), this, SLOT(handleInputMessages()));

    displaySettings();
    applySettings(true);

    // Get station position
    float stationLatitude = MainCore::instance()->getSettings().getLatitude();
    float stationLongitude = MainCore::instance()->getSettings().getLongitude();
    float stationAltitude = MainCore::instance()->getSettings().getAltitude();
    m_azEl.setLocation(stationLatitude, stationLongitude, stationAltitude);

    // Centre map at My Position
    QQuickItem *item = ui->map->rootObject();
    QObject *object = item->findChild<QObject*>("map");
    if (object != nullptr)
    {
        QGeoCoordinate coords = object->property("center").value<QGeoCoordinate>();
        coords.setLatitude(stationLatitude);
        coords.setLongitude(stationLongitude);
        object->setProperty("center", QVariant::fromValue(coords));
    }

    // Create antenna at My Position
    SWGSDRangel::SWGMapItem antennaMapItem;
    antennaMapItem.setName(new QString(MainCore::instance()->getSettings().getStationName()));
    antennaMapItem.setLatitude(stationLatitude);
    antennaMapItem.setLongitude(stationLongitude);
    antennaMapItem.setAltitude(stationAltitude);
    antennaMapItem.setImage(new QString("antenna.png"));
    antennaMapItem.setImageRotation(0);
    antennaMapItem.setImageMinZoom(11);
    antennaMapItem.setText(new QString(MainCore::instance()->getSettings().getStationName()));
    m_mapModel.update(m_map, &antennaMapItem, MapSettings::SOURCE_STATION);

    // Read beacons, if they exist
    QList<Beacon *> *beacons = Beacon::readIARUCSV(MapGUI::getBeaconFilename());
    if (beacons != nullptr)
        setBeacons(beacons);
}

MapGUI::~MapGUI()
{
    delete ui;
}

void MapGUI::setBeacons(QList<Beacon *> *beacons)
{
    delete m_beacons;
    m_beacons = beacons;
    m_beaconDialog.updateTable();
    // Add to Map
    QListIterator<Beacon *> i(*m_beacons);
    while (i.hasNext())
    {
        Beacon *beacon = i.next();
        SWGSDRangel::SWGMapItem beaconMapItem;
        beaconMapItem.setName(new QString(beacon->m_callsign));
        beaconMapItem.setLatitude(beacon->m_latitude);
        beaconMapItem.setLongitude(beacon->m_longitude);
        beaconMapItem.setAltitude(beacon->m_altitude);
        beaconMapItem.setImage(new QString("antenna.png"));
        beaconMapItem.setImageRotation(0);
        beaconMapItem.setImageMinZoom(8);
        beaconMapItem.setText(new QString(beacon->getText()));
        m_mapModel.update(m_map, &beaconMapItem, MapSettings::SOURCE_BEACONS);
    }
}

void MapGUI::blockApplySettings(bool block)
{
    m_doApplySettings = !block;
}

void MapGUI::applyMapSettings()
{
    QQuickItem *item = ui->map->rootObject();

    // Save existing position of map
    QObject *object = item->findChild<QObject*>("map");
    QGeoCoordinate coords;
    if (object != nullptr)
        coords = object->property("center").value<QGeoCoordinate>();

    // Create the map using the specified provider
    QQmlProperty::write(item, "mapProvider", m_settings.m_mapProvider);
    QVariantMap parameters;
    if (!m_settings.m_mapBoxApiKey.isEmpty() && m_settings.m_mapProvider == "mapbox")
    {
        parameters["mapbox.map_id"] = "mapbox.satellite"; // The only one that works
        parameters["mapbox.access_token"] = m_settings.m_mapBoxApiKey;
    }
    if (!m_settings.m_mapBoxApiKey.isEmpty() && m_settings.m_mapProvider == "mapboxgl")
    {
        parameters["mapboxgl.access_token"] = m_settings.m_mapBoxApiKey;
        if (!m_settings.m_mapBoxStyles.isEmpty())
            parameters["mapboxgl.mapping.additional_style_urls"] = m_settings.m_mapBoxStyles;
    }
    //QQmlProperty::write(item, "mapParameters", parameters);
    QMetaObject::invokeMethod(item, "createMap", Q_ARG(QVariant, QVariant::fromValue(parameters)));

    // Restore position of map
    object = item->findChild<QObject*>("map");
    if ((object != nullptr) && coords.isValid())
        object->setProperty("center", QVariant::fromValue(coords));

    // Get list of map types
    ui->mapTypes->clear();
    if (object != nullptr)
    {
        // Mapbox plugin only works for Satellite imagary, despite what is indicated
        if (m_settings.m_mapProvider == "mapbox")
            ui->mapTypes->addItem("Satellite");
        else
        {
            QVariant mapTypesVariant;
            QMetaObject::invokeMethod(item, "getMapTypes", Q_RETURN_ARG(QVariant, mapTypesVariant));
            QStringList mapTypes = mapTypesVariant.value<QStringList>();
            for (int i = 0; i < mapTypes.size(); i++)
                ui->mapTypes->addItem(mapTypes[i]);
        }
    }
}

void MapGUI::on_mapTypes_currentIndexChanged(int index)
{
    QVariant mapType = index;
    QMetaObject::invokeMethod(ui->map->rootObject(), "setMapType", Q_ARG(QVariant, mapType));
}

void MapGUI::displaySettings()
{
    setTitleColor(m_settings.m_rgbColor);
    setWindowTitle(m_settings.m_title);
    blockApplySettings(true);
    ui->displayNames->setChecked(m_settings.m_displayNames);
    m_mapModel.setDisplayNames(m_settings.m_displayNames);
    m_mapModel.setSources(m_settings.m_sources);
    applyMapSettings();
    blockApplySettings(false);
}

void MapGUI::leaveEvent(QEvent*)
{
}

void MapGUI::enterEvent(QEvent*)
{
}

void MapGUI::onMenuDialogCalled(const QPoint &p)
{
    if (m_contextMenuType == ContextMenuChannelSettings)
    {
        BasicFeatureSettingsDialog dialog(this);
        dialog.setTitle(m_settings.m_title);
        dialog.setColor(m_settings.m_rgbColor);
        dialog.setUseReverseAPI(m_settings.m_useReverseAPI);
        dialog.setReverseAPIAddress(m_settings.m_reverseAPIAddress);
        dialog.setReverseAPIPort(m_settings.m_reverseAPIPort);
        dialog.setReverseAPIFeatureSetIndex(m_settings.m_reverseAPIFeatureSetIndex);
        dialog.setReverseAPIFeatureIndex(m_settings.m_reverseAPIFeatureIndex);

        dialog.move(p);
        dialog.exec();

        m_settings.m_rgbColor = dialog.getColor().rgb();
        m_settings.m_title = dialog.getTitle();
        m_settings.m_useReverseAPI = dialog.useReverseAPI();
        m_settings.m_reverseAPIAddress = dialog.getReverseAPIAddress();
        m_settings.m_reverseAPIPort = dialog.getReverseAPIPort();
        m_settings.m_reverseAPIFeatureSetIndex = dialog.getReverseAPIFeatureSetIndex();
        m_settings.m_reverseAPIFeatureIndex = dialog.getReverseAPIFeatureIndex();

        setWindowTitle(m_settings.m_title);
        setTitleColor(m_settings.m_rgbColor);

        applySettings();
    }

    resetContextMenuType();
}

void MapGUI::applySettings(bool force)
{
    if (m_doApplySettings)
    {
        Map::MsgConfigureMap* message = Map::MsgConfigureMap::create(m_settings, force);
        m_map->getInputMessageQueue()->push(message);
    }
}

void MapGUI::on_maidenhead_clicked()
{
    MapMaidenheadDialog dialog;
    dialog.exec();
}

void MapGUI::on_displayNames_clicked(bool checked)
{
    m_settings.m_displayNames = checked;
    m_mapModel.setDisplayNames(checked);
}

void MapGUI::on_find_returnPressed()
{
    find(ui->find->text().trimmed());
}

void MapGUI::geoReply()
{
    QGeoCodeReply *pQGeoCode = dynamic_cast<QGeoCodeReply*>(sender());

    if ((pQGeoCode != nullptr) && (pQGeoCode->error() == QGeoCodeReply::NoError))
    {
        QList<QGeoLocation> qGeoLocs = pQGeoCode->locations();
        QQuickItem *item = ui->map->rootObject();
        QObject *map = item->findChild<QObject*>("map");
        if (qGeoLocs.size() == 1)
        {
            // Only one result, so centre map on that
            map->setProperty("center", QVariant::fromValue(qGeoLocs.at(0).coordinate()));
        }
        else if (qGeoLocs.size() == 0)
        {
            qDebug() << "MapGUI::geoReply: No location found for address";
            QApplication::beep();
        }
        else
        {
            // Show dialog allowing user to select from the results
            MapLocationDialog dialog(qGeoLocs);
            if (dialog.exec() == QDialog::Accepted)
                map->setProperty("center", QVariant::fromValue(dialog.m_selectedLocation.coordinate()));
        }
    }
    else
        qWarning() << "MapGUI::geoReply: GeoCode error: " << pQGeoCode->error();
    pQGeoCode->deleteLater();
}

void MapGUI::find(const QString& target)
{
    if (!target.isEmpty())
    {
        QQuickItem *item = ui->map->rootObject();
        QObject *map = item->findChild<QObject*>("map");
        if (map != nullptr)
        {
            // Search as:
            //  latitude and longitude
            //  Maidenhead locator
            //  object name
            //  address
            float latitude, longitude;
            if (Units::stringToLatitudeAndLongitude(target, latitude, longitude))
            {
                map->setProperty("center", QVariant::fromValue(QGeoCoordinate(latitude, longitude)));
            }
            else if (Maidenhead::fromMaidenhead(target, latitude, longitude))
            {
                map->setProperty("center", QVariant::fromValue(QGeoCoordinate(latitude, longitude)));
            }
            else
            {
                MapItem *mapItem = m_mapModel.findMapItem(target);
                if (mapItem != nullptr)
                    map->setProperty("center", QVariant::fromValue(mapItem->getCoordinates()));
                else
                {
                    QGeoServiceProvider* geoSrv = new QGeoServiceProvider("osm");
                    if (geoSrv != nullptr)
                    {
                        QLocale qLocaleC(QLocale::C, QLocale::AnyCountry);
                        geoSrv->setLocale(qLocaleC);
                        QGeoCodeReply *pQGeoCode = geoSrv->geocodingManager()->geocode(target);
                        if (pQGeoCode)
                            QObject::connect(pQGeoCode, &QGeoCodeReply::finished, this, &MapGUI::geoReply);
                        else
                            qDebug() << "MapGUI::find: GeoCoding failed";
                    }
                    else
                        qDebug() << "MapGUI::find: osm not available";
                }
            }
        }
    }
}

void MapGUI::on_deleteAll_clicked()
{
    m_mapModel.removeAll();
}

void MapGUI::on_displaySettings_clicked()
{
    MapSettingsDialog dialog(&m_settings);
    if (dialog.exec() == QDialog::Accepted)
    {
        if (dialog.m_mapSettingsChanged)
            applyMapSettings();
        applySettings();
        if (dialog.m_sourcesChanged)
            m_mapModel.setSources(m_settings.m_sources);
    }
}

void MapGUI::on_beacons_clicked()
{
    m_beaconDialog.show();
}

quint32 MapGUI::getSourceMask(const PipeEndPoint *sourcePipe)
{
    for (int i = 0; i < m_availablePipes.size(); i++)
    {
        if (m_availablePipes[i].m_source == sourcePipe)
        {
             for (int j = 0; j < MapSettings::m_pipeTypes.size(); j++)
             {
                 if (m_availablePipes[i].m_id == MapSettings::m_pipeTypes[j])
                     return 1 << j;
             }
        }
    }
    return 0;
}

QString MapGUI::getDataDir()
{
    // Get directory to store app data in (aircraft & airport databases and user-definable icons)
    QStringList locations = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation);
    // First dir is writable
    return locations[0];
}

QString MapGUI::getBeaconFilename()
{
    return MapGUI::getDataDir() + "/iaru_beacons.csv";
}
