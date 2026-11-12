import importlib.util
import pathlib
import unittest


SPEC = importlib.util.spec_from_file_location("sidebar_calendar", pathlib.Path(__file__).resolve().parents[1] / "generate_sidebar_calendar.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)

SAMPLE = """2026/2/17         1st Lunar Month         Tuesday
2026/4/5          18                      Sunday         Bright & Clear
2026/6/15         5th Lunar Month         Monday
2026/6/19         5                       Friday
2026/9/11         8th Lunar Month         Friday
2026/9/25         15                      Friday
"""


class SidebarCalendarGenerationTest(unittest.TestCase):
    def test_official_date_columns_produce_four_annual_days(self):
        year, dates = MODULE.extract_dates(2026, SAMPLE)
        self.assertEqual(year, 2026)
        self.assertEqual(dates, {"spring": 48, "qingming": 95, "dragon": 170, "autumn": 268})

    def test_leap_month_does_not_overwrite_the_regular_festival(self):
        text = SAMPLE.replace("2026/9/11", "2026/7/15         5th Lunar Month (Leap)  Wednesday\n2026/7/19         5                       Sunday\n2026/9/11")
        self.assertEqual(MODULE.extract_dates(2026, text)[1]["dragon"], 170)

    def test_unmarked_repeated_lunar_month_is_also_treated_as_leap(self):
        text = SAMPLE.replace("2026/9/11", "2026/7/15         5th Lunar Month         Wednesday\n2026/7/19         5                       Sunday\n2026/9/11")
        self.assertEqual(MODULE.extract_dates(2026, text)[1]["dragon"], 170)

    def test_missing_solar_term_is_rejected(self):
        with self.assertRaises(ValueError):
            MODULE.extract_dates(2026, SAMPLE.replace("Bright & Clear", ""))

    def test_html_error_page_cannot_become_calendar_data(self):
        with self.assertRaises(ValueError):
            MODULE.extract_dates(2026, "<html>Service unavailable</html>")


if __name__ == "__main__":
    unittest.main()
