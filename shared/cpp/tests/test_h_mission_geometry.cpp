#include <gtest/gtest.h>

#include "h_problem_core/planning/mission_geometry.h"

TEST(HMissionGeometry, EncodesCanonicalCells) {
    EXPECT_EQ(hcore::encodeCell(8, 0), "A9B1");
    EXPECT_EQ(hcore::encodeCell(0, 6), "A1B7");
}

TEST(HMissionGeometry, ConvertsFieldCentimetersToMissionMeters) {
    struct Case { hcore::PointCm input; double expected_x; double expected_y; };
    const std::vector<Case> cases{
        {{450.0, 350.0}, 0.0, 0.0}, {{50.0, 350.0}, 0.0, 4.0},
        {{450.0, 50.0}, 3.0, 0.0}, {{50.0, 50.0}, 3.0, 4.0},
        {{440.0, 330.0}, 0.2, 0.1},
    };
    for (const Case &item : cases) {
        const auto point = hcore::fieldPointToMissionMeters(item.input);
        EXPECT_DOUBLE_EQ(point.x_m, item.expected_x);
        EXPECT_DOUBLE_EQ(point.y_m, item.expected_y);
    }
}

TEST(HMissionGeometry, MapsA9B1ToMissionOrigin) {
    const auto center = hcore::cellCodeCenterCm("A9B1", 7);
    ASSERT_TRUE(center.has_value());
    const auto point = hcore::fieldPointToMissionMeters(*center);
    EXPECT_DOUBLE_EQ(point.x_m, 0.0);
    EXPECT_DOUBLE_EQ(point.y_m, 0.0);
}

TEST(HMissionGeometry, DecodesCellNumbersLikeQStringToInt) {
    const std::vector<std::string> compatible_codes{
        "A+1B1",
        "A 1B1",
        "A1B1 ",
        "A\t1B\n1 ",
    };
    for (const std::string &cell_code : compatible_codes) {
        SCOPED_TRACE(cell_code);
        const auto decoded = hcore::decodeCell(cell_code);
        ASSERT_TRUE(decoded.has_value());
        EXPECT_EQ(decoded->x, 0);
        EXPECT_EQ(decoded->y, 0);
    }

    for (const std::string &cell_code : {"A1 2B1", "A+ 1B1", "A1B1x"}) {
        SCOPED_TRACE(cell_code);
        EXPECT_FALSE(hcore::decodeCell(cell_code).has_value());
    }
}
