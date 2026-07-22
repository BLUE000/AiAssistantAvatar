#pragma once
#include <QString>
#include <QList>

namespace AIRandomUtils {
    /**
     * @brief min から max の閉区間 [min, max] で1つの整数をランダム抽選する。
     *        min > max の場合は自動的に反転・補正される。
     */
    int getRandom(int min, int max);

    /**
     * @brief 0 から max の閉区間 [0, max] から、重複しない整数を count 個取得する。
     *        max < 0 や count <= 0 の場合は空リストを返す。
     *        count > (max + 1) の場合は上限個数 (max + 1) まで取得する。
     */
    QList<int> getRandomList(int max, int count);

    /**
     * @brief 入力テキスト内に含まれる "Random(min, max)" や "RandomList(max, count)" マクロ式を自動解釈・評価し、置換文字列を返す。
     */
    QString parseAndEvaluate(const QString &text);
}
