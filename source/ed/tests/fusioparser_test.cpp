/* Copyright © 2001-2015, Canal TP and/or its affiliates. All rights reserved.

This file is part of Navitia,
    the software to build cool stuff with public transport.

Hope you'll enjoy and contribute to this project,
    powered by Canal TP (www.canaltp.fr).
Help us simplify mobility and open public transport:
    a non ending quest to the responsive locomotion way of traveling!

LICENCE: This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>.

Stay tuned using
twitter @navitia
IRC #navitia on freenode
https://groups.google.com/d/forum/navitia
www.navitia.io
*/

#include "ed/data.h"
#include "ed/connectors/gtfs_parser.h"
#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE test_ed
#include <boost/test/unit_test.hpp>
#include <string>
#include <boost/range/algorithm.hpp>
#include "conf.h"
#include "ed/build_helper.h"
#include "tests/utils_test.h"
#include "ed/connectors/fusio_parser.h"

struct logger_initialized {
    logger_initialized()   { init_logger(); }
};
BOOST_GLOBAL_FIXTURE( logger_initialized )

const std::string ntfs_path = std::string(navitia::config::fixtures_dir) + "/ed/ntfs";


std::ostream& operator<<(std::ostream& os, const nt::Header* h) {
    return os << h->uri;
}

BOOST_AUTO_TEST_CASE(parse_small_ntfs_dataset) {
    using namespace ed;

    Data data;
    connectors::FusioParser parser(ntfs_path);

    parser.fill(data);

    //we check that the data have been correctly loaded
    BOOST_REQUIRE_EQUAL(data.networks.size(), 1);
    BOOST_CHECK_EQUAL(data.networks[0]->name, "ligne flixible");
    BOOST_CHECK_EQUAL(data.networks[0]->uri, "ligneflexible");

    BOOST_REQUIRE_EQUAL(data.stop_areas.size(), 11);
    BOOST_CHECK_EQUAL(data.stop_areas[0]->name, "Arrêt A");
    BOOST_CHECK_EQUAL(data.stop_areas[0]->uri, "SA:A");
    BOOST_CHECK_CLOSE(data.stop_areas[0]->coord.lat(), 45.0296, 0.1);
    BOOST_CHECK_CLOSE(data.stop_areas[0]->coord.lon(), 0.5881, 0.1);

    //timzeone check
    //no timezone is given for the stop area in this dataset, to the agency time zone (the default one) is taken
    for (auto sa: data.stop_areas) {
        BOOST_CHECK_EQUAL(sa->time_zone_with_name.first, "Europe/Paris");
    }

    BOOST_REQUIRE_EQUAL(data.stop_points.size(), 8);
    BOOST_CHECK_EQUAL(data.stop_points[0]->uri, "SP:A");
    BOOST_CHECK_EQUAL(data.stop_points[0]->name, "Arrêt A");
    BOOST_CHECK_CLOSE(data.stop_points[0]->coord.lat(), 45.0296, 0.1);
    BOOST_CHECK_CLOSE(data.stop_points[0]->coord.lon(), 0.5881, 0.1);
    BOOST_CHECK_EQUAL(data.stop_points[0]->stop_area, data.stop_areas[0]);

    //we should have one zonal stop point
    BOOST_REQUIRE_EQUAL(boost::count_if(data.stop_points, [](const types::StopPoint* sp) {
                            return sp->is_zonal && sp->name == "Beleymas" && sp->area;
                        }), 1);

    BOOST_REQUIRE_EQUAL(data.lines.size(), 1);
    BOOST_REQUIRE_EQUAL(data.routes.size(), 3);

    //chekc comments
    BOOST_REQUIRE_EQUAL(data.comment_by_id.size(), 2);
    BOOST_CHECK_EQUAL(data.comment_by_id["bob"], "bob is in the kitchen");
    BOOST_CHECK_EQUAL(data.comment_by_id["bobette"], "test comment");

    //7 objects have comments
    //the file contains wrongly formated comments, but they are skiped
    BOOST_REQUIRE_EQUAL(data.comments.size(), 5);
    std::map<Data::comment_key, std::vector<std::string>> expected_comments = {
        {{"route", data.routes[0]}, {"bob"}},
        {{"trip", data.vehicle_journeys[4]}, {"bobette"}},
        {{"trip", data.vehicle_journeys[5]}, {"bobette"}}, //split too
        {{"stop_area", data.stop_areas[2]}, {"bob"}},
        {{"stop_point", data.stop_points[3]}, {"bobette"}}
    };

    BOOST_CHECK_EQUAL_COLLECTIONS(data.comments.begin(), data.comments.end(),
                                  expected_comments.begin(), expected_comments.end());


    std::map<const ed::types::StopTime*, std::vector<std::string>> expected_st_comments = {
            {{data.stops[6]}, {"bob", "bobette"}}, //stoptimes are split
            {{data.stops[7]}, {"bob", "bobette"}},
            {{data.stops[12]}, {"bob"}},
            {{data.stops[13]}, {"bob"}},
    };

    BOOST_CHECK_EQUAL_COLLECTIONS(data.stoptime_comments.begin(), data.stoptime_comments.end(),
                                  expected_st_comments.begin(), expected_st_comments.end());

}

