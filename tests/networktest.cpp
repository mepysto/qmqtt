#include "socketmock.h"
#include "timermock.h"
#include <qmqtt_network.h>
#include <QSignalSpy>
#include <QDataStream>
#include <QCoreApplication>
#include <gtest/gtest.h>

using namespace testing;

const QHostAddress HOST = QHostAddress::LocalHost;
const quint16 PORT = 3875;
const QIODevice::OpenMode OPEN_MODE = QIODevice::ReadWrite;

class NetworkTest : public Test
{
public:
    NetworkTest() {}
    virtual ~NetworkTest() {}

    void SetUp()
    {
        _byteArray.clear();
        _socketMock = new SocketMock;
        _timerMock = new TimerMock;
//        EXPECT_CALL(*_timerMock, setSingleShot(_)).WillRepeatedly(Return());
//        EXPECT_CALL(*_timerMock, setInterval(_)).WillRepeatedly(Return());
        _network.reset(new QMQTT::Network(_socketMock, _timerMock));
//        Mock::VerifyAndClearExpectations(_socketMock);
    }

    void TearDown()
    {
        _network.reset();
        _byteArray.clear();
    }

    qint64 readDataFromFixtureByteArray(char* data, qint64 requestedLength)
    {
        qint64 actualLength = qMin(requestedLength, static_cast<qint64>(_byteArray.size()));
        if(actualLength > 0)
        {
            QBuffer buffer(&_byteArray);
            buffer.open(QIODevice::ReadWrite);
            QDataStream in(&buffer);
            in.readRawData(data, actualLength);
            buffer.close();

            _byteArray.remove(0, actualLength);
        }
        return actualLength;
    }

    bool fixtureByteArrayIsEmpty() const
    {
        return _byteArray.isEmpty();
    }

    SocketMock* _socketMock;
    TimerMock* _timerMock;
    QSharedPointer<QMQTT::Network> _network;
    QByteArray _byteArray;
};

TEST_F(NetworkTest, networkConstructorDefaultValues_Test)
{
    EXPECT_FALSE(_network->autoReconnect());
    EXPECT_EQ(5000, _network->autoReconnectInterval());
}

TEST_F(NetworkTest, networkIsConnectedReturnsFalseWhenSocketStateIsUnconnectedState_Test)
{
    EXPECT_CALL(*_socketMock, state()).WillOnce(Return(QAbstractSocket::UnconnectedState));
    EXPECT_FALSE(_network->isConnectedToHost());
}

TEST_F(NetworkTest, networkIsConnectedReturnsTrueWhenSocketStateIsConnectedState_Test)
{
    EXPECT_CALL(*_socketMock, state()).WillOnce(Return(QAbstractSocket::ConnectedState));
    EXPECT_TRUE(_network->isConnectedToHost());
}

TEST_F(NetworkTest, networkStateReturnsUnconnectedStateWhenSocketStateIsUnconnectedState_Test)
{
    EXPECT_CALL(*_socketMock, state()).WillOnce(Return(QAbstractSocket::UnconnectedState));
    EXPECT_EQ(QAbstractSocket::UnconnectedState, _network->state());
}

TEST_F(NetworkTest, networkStateReturnsConnectedStateWhenSocketStateIsConnectedState_Test)
{
    EXPECT_CALL(*_socketMock, state()).WillOnce(Return(QAbstractSocket::ConnectedState));
    EXPECT_EQ(QAbstractSocket::ConnectedState, _network->state());
}

TEST_F(NetworkTest, networkConnectToHostCallsSocketConnectToHost_Test)
{
    EXPECT_CALL(*_socketMock, connectToHost(HOST, PORT));
    _network->connectToHost(HOST, PORT);
}

TEST_F(NetworkTest, networkDisconnectFromHostCallsSocketDisconnectFromHost_Test)
{
    EXPECT_CALL(*_socketMock, disconnectFromHost());
    _network->disconnectFromHost();
}

TEST_F(NetworkTest, networkStateCallsSocketState_Test)
{
    EXPECT_CALL(*_socketMock, state()).WillOnce(Return(QAbstractSocket::ConnectedState));
    EXPECT_EQ(QAbstractSocket::ConnectedState, _network->state());
}

TEST_F(NetworkTest, networkSetAutoReconnectTrueSetsAutoReconnectTrue_Test)
{
    _network->setAutoReconnect(true);
    EXPECT_TRUE(_network->autoReconnect());
}

TEST_F(NetworkTest, networkSendFrameWillNotSendAFrameIfNotConnected_Test)
{
    EXPECT_CALL(*_socketMock, state()).WillOnce(Return(QAbstractSocket::UnconnectedState));
    EXPECT_CALL(*_socketMock, writeData(_, _)).Times(0);

    QMQTT::Frame frame;
    _network->sendFrame(frame);
}

TEST_F(NetworkTest, networkSendFrameWillSendAFrameIfConnected_Test)
{
    EXPECT_CALL(*_socketMock, state()).WillOnce(Return(QAbstractSocket::ConnectedState));
    EXPECT_CALL(*_socketMock, writeData(_, _));

    QMQTT::Frame frame;
    _network->sendFrame(frame);
}

TEST_F(NetworkTest, networkEmitsConnectedSignalWhenSocketEmitsConnectedSignal_Test)
{
    QSignalSpy spy(_network.data(), &QMQTT::Network::connected);
    emit _socketMock->connected();
    EXPECT_EQ(1, spy.count());
}

TEST_F(NetworkTest, networkEmitsDisconnectedSignalWhenSocketEmitsDisconnectedSignal_Test)
{
    QSignalSpy spy(_network.data(), &QMQTT::Network::disconnected);
    emit _socketMock->disconnected();
    EXPECT_EQ(1, spy.count());
}

TEST_F(NetworkTest, networkEmitsReceivedSignalOnceAFrameIsReceived_Test)
{    
    QBuffer buffer(&_byteArray);
    buffer.open(QIODevice::WriteOnly);
    QDataStream out(&buffer);
    QMQTT::Frame frame;
    frame._header = 42;
    frame._data = QString("data").toUtf8();
    out << frame;
    buffer.close();

    ASSERT_EQ(6, _byteArray.size());

    EXPECT_CALL(*_socketMock, atEnd())
        .WillRepeatedly(Invoke(this, &NetworkTest::fixtureByteArrayIsEmpty));
    EXPECT_CALL(*_socketMock, readData(_, _))
        .WillRepeatedly(Invoke(this, &NetworkTest::readDataFromFixtureByteArray));

    QSignalSpy spy(_network.data(), &QMQTT::Network::received);
    emit _socketMock->readyRead();
    ASSERT_EQ(1, spy.count());
    EXPECT_EQ(frame, spy.at(0).at(0).value<QMQTT::Frame>());
}

TEST_F(NetworkTest, networkWillAttemptToReconnectOnDisconnectionIfAutoReconnectIsTrue_Test)
{
    EXPECT_CALL(*_timerMock, start()).WillRepeatedly(DoAll(
        Invoke(_timerMock, &QMQTT::TimerInterface::timeout),
        Return()));
    _network->setAutoReconnect(true);

    EXPECT_CALL(*_socketMock, connectToHost(_, _));
    emit _socketMock->disconnected();
}

TEST_F(NetworkTest, networkWillNotAttemptToReconnectOnDisconnectionIfAutoReconnectIsFalse_Test)
{
    EXPECT_CALL(*_timerMock, start()).WillRepeatedly(DoAll(
        Invoke(_timerMock, &QMQTT::TimerInterface::timeout),
        Return()));
    _network->setAutoReconnect(false);

    EXPECT_CALL(*_socketMock, connectToHost(_, _)).Times(0);
    emit _socketMock->disconnected();
}

TEST_F(NetworkTest, networkWillAttemptToReconnectOnConnectionErrorIfAutoReconnectIsTrue_Test)
{
    EXPECT_CALL(*_timerMock, start()).WillRepeatedly(DoAll(
        Invoke(_timerMock, &QMQTT::TimerInterface::timeout),
        Return()));
    _network->setAutoReconnect(true);

    EXPECT_CALL(*_socketMock, connectToHost(_, _));
    _socketMock->error(QAbstractSocket::ConnectionRefusedError);
}

TEST_F(NetworkTest, networkWillNotAttemptToReconnectOnConnectionErrorIfAutoReconnectIsFalse_Test)
{
    EXPECT_CALL(*_timerMock, start()).WillRepeatedly(DoAll(
        Invoke(_timerMock, &QMQTT::TimerInterface::timeout),
        Return()));
    _network->setAutoReconnect(false);

    EXPECT_CALL(*_socketMock, connectToHost(_, _)).Times(0);
    _socketMock->error(QAbstractSocket::ConnectionRefusedError);
}

TEST_F(NetworkTest, networkWillEmitErrorOnSocketError_Test)
{
    EXPECT_CALL(*_timerMock, start()).WillRepeatedly(DoAll(
        Invoke(_timerMock, &QMQTT::TimerInterface::timeout),
        Return()));

    QSignalSpy spy(_network.data(), &QMQTT::NetworkInterface::error);
    _socketMock->error(QAbstractSocket::ConnectionRefusedError);
    EXPECT_EQ(1, spy.count());
    EXPECT_EQ(QAbstractSocket::ConnectionRefusedError,
              spy.at(0).at(0).value<QAbstractSocket::SocketError>());
}
