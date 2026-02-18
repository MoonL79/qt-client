
#include <QString>

#ifndef CONFIG_H
#define CONFIG_H

class Config {

public:
  static QString GetHost();
  static quint16 GetPort();

private:
  const QString host_ip;
  const quint16 host_port;
};

#endif // CONFIG_H
