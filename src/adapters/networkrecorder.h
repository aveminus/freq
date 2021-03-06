#ifndef NETWORKRECORDER_H
#define NETWORKRECORDER_H

#include "signal/recorder.h"

#include <QUrl>
#include <QTcpSocket>
#include <QHostAddress>

namespace Adapters {

/**
  To use 'NetworkRecorder' start Sonic AWE with the syntax below and hit the regular 'record' button.

  sonicawe s16bursts://hostname:hostport

  Example:

  sonicawe s16bursts://123.45.67.89:12345


  NetworkRecorder only supports data in signed 16-bit integers.
  */
class NetworkRecorder: public QObject, public Signal::Recorder
{
    Q_OBJECT
public:
    NetworkRecorder(QUrl url, float samplerate=32768 );
    ~NetworkRecorder();

    virtual void startRecording() override;
    virtual void stopRecording() override;
    virtual bool isStopped() const override;
    virtual bool canRecord() override;
    virtual std::string name() override;
    virtual float length() override;

private:
    QUrl url;
    QTcpSocket tcpSocket;

    virtual float time() override;
    int receivedData(const void*data, int byteCount);

private slots:
    void readyRead();
    void connected();
    void disconnected();
    void hostFound();
    void error(QAbstractSocket::SocketError);
    void stateChanged(QAbstractSocket::SocketState);
};

} // namespace Adapters

#endif // NETWORKRECORDER_H
