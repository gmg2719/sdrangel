///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2020 Edouard Griffiths, F4EXB.                                  //
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

#ifndef INCLUDE_SIGMFFILESINK_H_
#define INCLUDE_SIGMFFILESINK_H_

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QNetworkRequest>

#include "dsp/basebandsamplesink.h"
#include "channel/channelapi.h"

#include "sigmffilesinksettings.h"

class QNetworkAccessManager;
class QNetworkReply;

class DeviceAPI;
class DeviceSampleSource;
class SigMFFileSinkBaseband;

class SigMFFileSink : public BasebandSampleSink, public ChannelAPI {
    Q_OBJECT
public:
    class MsgConfigureSigMFFileSink : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        const SigMFFileSinkSettings& getSettings() const { return m_settings; }
        bool getForce() const { return m_force; }

        static MsgConfigureSigMFFileSink* create(const SigMFFileSinkSettings& settings, bool force)
        {
            return new MsgConfigureSigMFFileSink(settings, force);
        }

    private:
        SigMFFileSinkSettings m_settings;
        bool m_force;

        MsgConfigureSigMFFileSink(const SigMFFileSinkSettings& settings, bool force) :
            Message(),
            m_settings(settings),
            m_force(force)
        { }
    };

    SigMFFileSink(DeviceAPI *deviceAPI);
    virtual ~SigMFFileSink();
    virtual void destroy() { delete this; }

    virtual void feed(const SampleVector::const_iterator& begin, const SampleVector::const_iterator& end, bool po);
    virtual void start();
    virtual void stop();
    virtual bool handleMessage(const Message& cmd);

    virtual void getIdentifier(QString& id) { id = objectName(); }
    virtual void getTitle(QString& title) { title = "SigMF File Sink"; }
    virtual qint64 getCenterFrequency() const { return m_frequencyOffset; }

    virtual QByteArray serialize() const;
    virtual bool deserialize(const QByteArray& data);

    virtual int getNbSinkStreams() const { return 1; }
    virtual int getNbSourceStreams() const { return 0; }

    virtual qint64 getStreamCenterFrequency(int streamIndex, bool sinkElseSource) const
    {
        (void) streamIndex;
        (void) sinkElseSource;
        return m_frequencyOffset;
    }

    virtual int webapiSettingsGet(
            SWGSDRangel::SWGChannelSettings& response,
            QString& errorMessage);

    virtual int webapiSettingsPutPatch(
            bool force,
            const QStringList& channelSettingsKeys,
            SWGSDRangel::SWGChannelSettings& response,
            QString& errorMessage);

    static void webapiFormatChannelSettings(
        SWGSDRangel::SWGChannelSettings& response,
        const SigMFFileSinkSettings& settings);

    static void webapiUpdateChannelSettings(
            SigMFFileSinkSettings& settings,
            const QStringList& channelSettingsKeys,
            SWGSDRangel::SWGChannelSettings& response);

    void getLocalDevices(std::vector<uint32_t>& indexes);
    uint32_t getNumberOfDeviceStreams() const;
    void record(bool record);
    uint64_t getMsCount() const;
    uint64_t getByteCount() const;
    unsigned int getNbTracks() const;

    static const QString m_channelIdURI;
    static const QString m_channelId;

private:
    DeviceAPI *m_deviceAPI;
    QThread m_thread;
    SigMFFileSinkBaseband *m_basebandSink;
    SigMFFileSinkSettings m_settings;

    uint64_t m_centerFrequency;
    int64_t m_frequencyOffset;
    uint32_t m_basebandSampleRate;

    QNetworkAccessManager *m_networkManager;
    QNetworkRequest m_networkRequest;

    void applySettings(const SigMFFileSinkSettings& settings, bool force = false);
    void propagateSampleRateAndFrequency(uint32_t index, uint32_t log2Decim);
    DeviceSampleSource *getLocalDevice(uint32_t index);

    void webapiReverseSendSettings(QList<QString>& channelSettingsKeys, const SigMFFileSinkSettings& settings, bool force);

private slots:
    void networkManagerFinished(QNetworkReply *reply);
};

#endif /* INCLUDE_SIGMFFILESINK_H_ */
