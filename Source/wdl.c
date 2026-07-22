#include "wdl.h"

#include <math.h>
#include "utils.h"

static wdl_params_t wdl_params(uint16_t material) {
    static const double kAs[4] = {-132.82800816, 372.72079222, -354.81691410, 329.92413018};
    static const double kBs[4] = {68.24072080, -111.17718819, 74.50316570, 71.16566713};

    const double m = (double)clamp(material, (uint16_t)17, (uint16_t)78) / 58.0;

    wdl_params_t result;
    result.a = ((kAs[0] * m + kAs[1]) * m + kAs[2]) * m + kAs[3];
    result.b = ((kBs[0] * m + kBs[1]) * m + kBs[2]) * m + kBs[3];

    return result;
}

wdl_model_result_t wdl_model(int16_t pov_score, uint16_t material) {
    const wdl_params_t params = wdl_params(material);
    const double a = params.a;
    const double b = params.b;

    const double x = (double)pov_score;

    wdl_model_result_t result;
    result.win = (int32_t)llround(1000.0 / (1.0 + exp((a - x) / b)));
    result.loss = (int32_t)llround(1000.0 / (1.0 + exp((a + x) / b)));

    return result;
}

int16_t wdl_normalize_score(int16_t score, uint16_t material) {
    if (score == 0 || is_decisive(score)) {
        return score;
    }

    const wdl_params_t params = wdl_params(material);
    const double a = params.a;

    const double normalized = (double)score / a;

    return (int16_t)llround(100.0 * normalized);
}

int16_t wdl_unnormalize_score(int16_t score, uint16_t material) {
    const wdl_params_t params = wdl_params(material);
    const double a = params.a;

    const double unnormalized = (double)score * a / 100.0;
    return (int16_t)llround(unnormalized);
}
