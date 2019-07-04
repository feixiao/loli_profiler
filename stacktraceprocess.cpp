#include "stacktraceprocess.h"
#include "timeprofiler.h"

#include "lz4/lz4.h"

#include <QtEndian>
#include <QTcpSocket>
#include <QTextStream>
#include <QDataStream>
#include <QRegularExpression>
#include <QProcess>
#include <QDebug>

#define BUFFER_SIZE 65535

StackTraceProcess::StackTraceProcess(QObject* parent)
    : QObject(parent), socket_(new QTcpSocket(this)) {
    buffer_ = new char[BUFFER_SIZE];
    compressBuffer_ = new char[compressBufferSize_];
    socket_->setReadBufferSize(BUFFER_SIZE);
    connect(socket_, &QTcpSocket::readyRead, this, &StackTraceProcess::OnDataReceived);
    connect(socket_, &QTcpSocket::connected, this, &StackTraceProcess::OnConnected);
    connect(socket_, &QTcpSocket::disconnected, this, &StackTraceProcess::OnDisconnected);
}

StackTraceProcess::~StackTraceProcess(){
    delete[] buffer_;
    delete[] compressBuffer_;
}

void StackTraceProcess::ConnectToServer(int port) {
    QProcess process;
    QStringList arguments;
    arguments << "forward" << "tcp:" + QString::number(port) << "tcp:7100";
    process.start(execPath_, arguments);
    if (!process.waitForStarted()) {
        emit ConnectionLost();
        return;
    }
    if (!process.waitForFinished()) {
        emit ConnectionLost();
        return;
    }
    connectingServer_ = true;
    socket_->connectToHost("127.0.0.1", static_cast<quint16>(port));
}

void StackTraceProcess::Disconnect() {
    socket_->close();
    packetSize_ = 0;
}

enum loliFlags {
    FREE_ = 0,
    MALLOC_ = 1,
    CALLOC_ = 2,
};

void StackTraceProcess::Interpret(const QByteArray& bytes) {
    quint32 originSize = *reinterpret_cast<const quint32*>(bytes.data());
//    qDebug() << "originSize: " << originSize;
    if (originSize > compressBufferSize_) {
        compressBufferSize_ = static_cast<quint32>(originSize * 1.5f);
        delete[] compressBuffer_;
        compressBuffer_ = new char[compressBufferSize_];
    }
    auto decompressSize = LZ4_decompress_safe(bytes.data() + 4, compressBuffer_, bytes.size() - 4, static_cast<qint32>(compressBufferSize_));
    if (decompressSize == 0) {
        qDebug() << "LZ4 decompression failed!";
        return;
    }
    freeInfo_.clear();
    stackInfo_.clear();
    QByteArray uncompressedBytes = QByteArray::fromRawData(compressBuffer_, decompressSize);
    QTextStream stream(uncompressedBytes);
    QString line;
    int lineCount = 0;
    while (stream.readLineInto(&line)) {
        auto words = line.split('\\');
        if (words.size() == 0)
            continue;
        auto type = words[0].toInt();
        if (type == FREE_) {
            if (words.size() < 3)
                continue;
            freeInfo_.push_back(qMakePair(words[1].toInt(), words[2]));
        } else {
            words.removeAt(0);
            stackInfo_.push_back(words);
        }
        lineCount++;
    }
//    qDebug() << "OnDataArrived: " << bytes.size() << " bytes " << " lines: " << lineCount << " stacks: " << stackInfo_.size() << " frees: " << freeInfo_.size();
    emit DataReceived();
}

void StackTraceProcess::OnDataReceived() {
    auto size = socket_->read(buffer_, BUFFER_SIZE);
    if (size <= 0)
        return;
    qint64 bufferPos = 0; // pos = size - remains
    qint64 remainBytes = size;
    while (remainBytes > 0) {
        if (packetSize_ == 0) {
            packetSize_ = *reinterpret_cast<quint32*>(buffer_ + bufferPos);
//            qDebug() << "receiving: " <<  packetSize_;
            remainBytes -= 4;
            bufferPos = size - remainBytes;
            bufferCache_.resize(0);
            if (remainBytes > 0) {
                if (packetSize_ < remainBytes) { // the data is stored in the same packet, then read them all
                    bufferCache_.append(buffer_ + bufferPos, static_cast<int>(packetSize_));
                    remainBytes -= packetSize_;
                    bufferPos = size - remainBytes;
                    Interpret(bufferCache_);
                    bufferCache_.resize(0);
                    packetSize_ = 0;
                } else { // the data is splited to another packet, we just append whatever we got
                    bufferCache_.append(buffer_ + bufferPos, static_cast<int>(remainBytes));
                    break;
                }
            }
        } else {
            auto remainPacketSize = packetSize_ - static_cast<uint>(bufferCache_.size());
            if (remainPacketSize < remainBytes) { // the remaining data is stored in this packet, read what we need
                bufferCache_.append(buffer_ + bufferPos, static_cast<int>(remainPacketSize));
                remainBytes -= remainPacketSize;
                bufferPos = size - remainBytes;
                Interpret(bufferCache_);
                bufferCache_.resize(0);
                packetSize_ = 0;
            } else { // the remaining data is splited to another packet, we just append whatever we got
                bufferCache_.append(buffer_ + bufferPos, static_cast<int>(remainBytes));
                break;
            }
        }
    }
}

void StackTraceProcess::OnConnected() {
    connectingServer_ = false;
    serverConnected_ = true;
}

void StackTraceProcess::OnDisconnected() {
    connectingServer_ = false;
    serverConnected_ = false;
    emit ConnectionLost();
}