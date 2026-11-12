#pragma once
#include "Tuning.h"

namespace comma
{
// MTS-ESP (ODDSound): клиент получает строй от мастера, по желанию Comma сам становится мастером.
// Нужна библиотека libMTS, которую ставит любой мастер MTS-ESP (например, бесплатный MTS-ESP Mini).
// Без неё клиент молчит (нет мастера), а мастер не регистрируется. Все вызовы — из потока сообщений.
class MTSLink
{
public:
    MTSLink();
    ~MTSLink();

    bool hasMaster() const;
    // строй от мастера: true, если он сменился с прошлого раза
    bool readTuning (std::array<double, 128>& hz, juce::String& name, double& periodCents);

    bool canRegisterMaster() const;
    bool setMaster (bool on);       // false — не вышло (уже есть другой мастер или нет libMTS)
    bool isMaster() const { return master; }
    void pushTuning (const TuningSnapshot& t);

private:
    void* client = nullptr;
    bool master = false;
    std::array<double, 128> last {};
};
} // namespace comma
