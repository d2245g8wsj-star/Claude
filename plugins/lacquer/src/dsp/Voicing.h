#pragma once

#include "Band.h"

#include <array>
#include <cmath>

namespace lacquer
{

/** The switch positions of the six bands, as the published factsheet and
    product page list them, and what each position does to the filter network.

    Frequencies are exact. Everything else here - gain at full turn, the taper,
    the Q of each section and what Shift changes - is not published, and is
    this project's estimate from the circuit families the bands come from and
    from what reviewers report hearing. Each estimate is marked where it is
    made, so it can be retuned in one place.
*/
namespace voicing
{
    /** Each amount control is a stepped switch: 0 to 10 in half steps. */
    inline constexpr int amountSteps = 20;

    /** The treble Q control: 0 (wide bell, soft shelf knee) to 10 (narrow bell,
        hard knee), in whole steps. */
    inline constexpr int qSteps = 10;

    // ---- Bass boost: passive resonant shelf (Pultec family) -------------
    inline constexpr std::array<float, 6> lowBoostHz  { 47.0f, 70.0f, 100.0f, 160.0f, 200.0f, 340.0f };

    // ---- Bass cut: passive shelf (Pultec family) ------------------------
    inline constexpr std::array<float, 6> lowCutHz    { 27.0f, 50.0f, 80.0f, 140.0f, 220.0f, 480.0f };

    // ---- Mid boost: passive broad bell (Neumann PEV family) -------------
    inline constexpr std::array<float, 11> midBoostHz { 310.0f, 470.0f, 680.0f, 820.0f, 1000.0f, 1800.0f,
                                                        2500.0f, 3000.0f, 4000.0f, 6300.0f, 8200.0f };

    // ---- Mid cut: active bell whose Q narrows as it cuts; position 0 is OUT
    inline constexpr std::array<float, 11> midCutHz   { 0.0f, 260.0f, 470.0f, 820.0f, 1000.0f, 1300.0f,
                                                        2100.0f, 3000.0f, 4500.0f, 6300.0f, 9300.0f };

    // ---- Treble boost: seven inductor bells, then five RC shelves -------
    inline constexpr int trebleBells = 7;
    inline constexpr std::array<float, 12> trebleBoostHz { 2500.0f, 4200.0f, 8000.0f, 10000.0f, 10000.0f, 13900.0f, 18000.0f,
                                                           18000.0f, 15000.0f, 10000.0f, 6000.0f, 1000.0f };

    /** The two 10 kHz bells use different inductors. A series LC section's Q
        goes with sqrt(L/C), so the low inductance coil is the wider bell. */
    inline constexpr std::array<float, trebleBells> trebleBellInductance { 1.0f, 1.0f, 1.0f, 0.72f, 1.38f, 1.0f, 1.0f };

    // ---- Treble cut: passive shelf --------------------------------------
    inline constexpr std::array<float, 6> trebleCutHz { 4000.0f, 6000.0f, 10000.0f, 12000.0f, 18000.0f, 24000.0f };

    /** Gain at the last step, per band. Estimates: the bass bands follow the
        classic passive program equaliser's published maxima, the rest are set
        so the full turn of each is a similar "too much". */
    inline constexpr float lowBoostMaxDb    =  13.0f;
    inline constexpr float lowCutMaxDb      = -16.0f;
    inline constexpr float midBoostMaxDb    =  10.0f;
    inline constexpr float midCutMaxDb      = -14.0f;
    inline constexpr float trebleBoostMaxDb =  15.0f;
    inline constexpr float trebleCutMaxDb   = -16.0f;

    /** Reviewers report the amount switches do little until most of the way
        round, as a passive network on a linear pot does. A square law gives
        that: a quarter of the gain at half travel. */
    inline float amountDb (int step, float maxDb) noexcept
    {
        const auto t = (float) std::clamp (step, 0, amountSteps) / (float) amountSteps;
        return maxDb * t * t;
    }

    inline bool trebleIsBell (int position) noexcept { return position < trebleBells; }

    /** Every control of one channel, as switch positions. */
    struct Settings
    {
        int  lowBoostFreq { 2 }, lowBoostAmount { 0 };
        int  lowCutFreq   { 1 }, lowCutAmount   { 0 };
        int  midBoostFreq { 4 }, midBoostAmount { 0 };
        int  midCutFreq   { 0 }, midCutAmount   { 0 };
        int  trebleBoostFreq { 3 }, trebleQ { 5 }, trebleBoostAmount { 0 };
        int  trebleCutFreq   { 3 }, trebleCutAmount { 0 };
        bool shift { false };
    };

    struct Target
    {
        float hz, db, q;
    };

    /** The seven sections of the network, in the order the signal meets them.
        The treble boost is two sections, a bell and a shelf, of which only the
        selected one has gain; switching between them is a glide rather than a
        jump in filter type. */
    enum Section { lowBoost = 0, lowCut, midBoost, midCut, trebleBell, trebleShelf, trebleCut, numSections };

    inline constexpr std::array<Band::Type, numSections> sectionTypes {
        Band::Type::lowShelf, Band::Type::lowShelf, Band::Type::bell, Band::Type::bell,
        Band::Type::bell, Band::Type::highShelf, Band::Type::highShelf
    };

    template <size_t N>
    float pick (const std::array<float, N>& table, int index) noexcept
    {
        return table[(size_t) std::clamp (index, 0, (int) N - 1)];
    }

    inline std::array<Target, numSections> targets (const Settings& s) noexcept
    {
        std::array<Target, numSections> t {};

        // Bass. The classic set is the soft, wide shelf; Shift swaps in the
        // second capacitor set, which moves the corners and tightens both
        // shelves so the boost grows a resonant shoulder and the cut a sharper
        // knee. The boost section's Q also rises with the boost, as a passive
        // LC shelf's does when less of it is shunted away.
        {
            const auto boost01 = (float) s.lowBoostAmount / (float) amountSteps;
            const auto boostHz = pick (lowBoostHz, s.lowBoostFreq) * (s.shift ? 0.82f : 1.0f);
            const auto boostQ  = s.shift ? 0.95f + 0.45f * boost01 : 0.55f + 0.30f * boost01;
            t[lowBoost] = { boostHz, amountDb (s.lowBoostAmount, lowBoostMaxDb), boostQ };

            const auto cutHz = pick (lowCutHz, s.lowCutFreq) * (s.shift ? 1.18f : 1.0f);
            t[lowCut] = { cutHz, amountDb (s.lowCutAmount, lowCutMaxDb), s.shift ? 0.80f : 0.50f };
        }

        // Mid boost: broad and gentle, nearly constant in width.
        {
            const auto boost01 = (float) s.midBoostAmount / (float) amountSteps;
            t[midBoost] = { pick (midBoostHz, s.midBoostFreq), amountDb (s.midBoostAmount, midBoostMaxDb),
                            0.62f * (1.0f + 0.2f * boost01) };
        }

        // Mid cut: the active band. Its "dynamic" Q narrows as the cut deepens,
        // so a little is a broad scoop and a lot is a notch-like dip.
        {
            const auto out   = s.midCutFreq <= 0;
            const auto cut01 = (float) s.midCutAmount / (float) amountSteps;
            t[midCut] = { out ? 1000.0f : pick (midCutHz, s.midCutFreq),
                          out ? 0.0f : amountDb (s.midCutAmount, midCutMaxDb),
                          0.55f + 2.2f * cut01 };
        }

        // Treble boost: a bell through an inductor, or a shelf without one.
        // Q is the bell's width, or the shelf's knee.
        {
            const auto q01  = (float) std::clamp (s.trebleQ, 0, qSteps) / (float) qSteps;
            const auto hz   = pick (trebleBoostHz, s.trebleBoostFreq);
            const auto db   = amountDb (s.trebleBoostAmount, trebleBoostMaxDb);
            const auto bell = trebleIsBell (s.trebleBoostFreq);
            const auto boost01 = (float) s.trebleBoostAmount / (float) amountSteps;

            const auto inductance = bell ? trebleBellInductance[(size_t) std::clamp (s.trebleBoostFreq, 0, trebleBells - 1)]
                                         : 1.0f;
            const auto bellQ  = (0.45f * std::pow (5.0f, q01)) * inductance * (1.0f + 0.25f * boost01);
            const auto shelfQ = 0.50f + 0.60f * q01;

            // The idle section sits where the active one is, so a change of
            // type only fades one shape out as the other fades in.
            t[trebleBell]  = { hz, bell ? db : 0.0f, bellQ };
            t[trebleShelf] = { hz, bell ? 0.0f : db, shelfQ };
        }

        t[trebleCut] = { pick (trebleCutHz, s.trebleCutFreq), amountDb (s.trebleCutAmount, trebleCutMaxDb), 0.50f };

        return t;
    }
}

} // namespace lacquer
