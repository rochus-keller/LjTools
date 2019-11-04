#ifndef STREAMSPY
#define STREAMSPY

#include <QIODevice>

class OutStreamSpy : public QIODevice
{
public:
    OutStreamSpy( QIODevice* d ):d_dev(d),d_on(false){ open(WriteOnly|Unbuffered); }
    void setOn(bool on) { d_on = on;}
    void setCatch( const QByteArray& c ) { d_catch = c; }
protected:
    qint64	readData(char * data, qint64 maxSize) { return -1; }
    qint64	writeData(const char * data, qint64 maxSize)
    {
        if( d_catch.size() == maxSize && QByteArray::fromRawData(data,maxSize) == d_catch )
            qDebug() << "hit catch";
        if( d_on )
            qDebug() << QByteArray::fromRawData(data,maxSize).toHex().constData();
        return d_dev->write(data,maxSize);
    }

private:
    bool d_on;
    QByteArray d_catch;
    QIODevice* d_dev;
};

#endif // STREAMSPY

