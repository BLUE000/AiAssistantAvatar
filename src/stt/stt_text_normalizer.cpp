#include "stt_text_normalizer.h"
#include "../utils/wakeword_matcher.h"

QString STTTextNormalizer::normalizePhonetics(const QString &inputText) {
    if (inputText.isEmpty()) return inputText;

    QString s = WakewordMatcher::toKatakana(inputText);
    // 四つ仮名統一
    s.replace(QChar(0x30C2), QChar(0x30B8)); // ヂ -> ジ
    s.replace(QChar(0x30C5), QChar(0x30BA)); // ヅ -> ズ

    return s;
}
