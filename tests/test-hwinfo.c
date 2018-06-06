/* Copyright 2018 Endless Mobile, Inc. */

/* This file is part of eos-metrics-instrumentation.
 *
 * eos-metrics-instrumentation is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or (at your
 * option) any later version.
 *
 * eos-metrics-instrumentation is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with eos-metrics-instrumentation.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include "eins-hwinfo.h"

static void
test_get_disk_space_for_root (void)
{
  g_autoptr(GFile) root = g_file_new_for_path ("/");
  g_autoptr(GVariant) payload = NULL;
  g_autoptr(GError) error = NULL;
  guint32 total, used, available;

  payload = eins_hwinfo_get_disk_space_for_partition (root, &error);
  g_assert_no_error (error);
  g_assert_nonnull (payload);

  g_assert_cmpstr (g_variant_get_type_string (payload), ==, "(uuu)");
  g_variant_get (payload, "(uuu)", &total, &used, &available);

  g_assert_cmpuint (total, >, 0);
  g_assert_cmpuint (used, >, 0);
  /* maybe you have < 500 MB free, so no assertion about available itself */

  /* since we round to the nearest gigabyte, used + available <= total may not
   * hold -- what if used and available round up, but total rounds down? -- but
   * we should be within 1 GB.
   */
  g_assert_cmpuint (used + available, <=, total + 1);
}

static void
test_get_disk_space_for_nonexistent_dir (void)
{
  g_autoptr(GFile) nonexistent = NULL;
  g_autoptr(GVariant) payload = NULL;
  g_autoptr(GError) error = NULL;
  guint32 total, used, available;

  nonexistent = g_file_new_for_path ("/ca29d735-ca59-4774-8677-5bf3e9f34a7e");

  payload = eins_hwinfo_get_disk_space_for_partition (nonexistent, &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_assert_null (payload);
}

static void
test_get_ram_size_for_current_system (void)
{
  g_autoptr(GVariant) payload = eins_hwinfo_get_ram_size ();
  guint32 size;

  g_assert_nonnull (payload);
  g_assert_cmpstr (g_variant_get_type_string (payload), ==, "u");
  g_variant_get (payload, "u", &size);
  /* If you have a system with less than 100 MB of RAM this test will fail.
   * Good luck running Endless OS on that!
   */
  g_assert_cmpuint (size, >=, 100);
}

static const char *XPS_13_9343_JSON =
    "{"
    "   \"lscpu\": ["
    "      {\"field\": \"Architecture:\", \"data\": \"x86_64\"},"
    "      {\"field\": \"CPU op-mode(s):\", \"data\": \"32-bit, 64-bit\"},"
    "      {\"field\": \"Byte Order:\", \"data\": \"Little Endian\"},"
    "      {\"field\": \"CPU(s):\", \"data\": \"4\"},"
    "      {\"field\": \"On-line CPU(s) list:\", \"data\": \"0-3\"},"
    "      {\"field\": \"Thread(s) per core:\", \"data\": \"2\"},"
    "      {\"field\": \"Core(s) per socket:\", \"data\": \"2\"},"
    "      {\"field\": \"Socket(s):\", \"data\": \"1\"},"
    "      {\"field\": \"NUMA node(s):\", \"data\": \"1\"},"
    "      {\"field\": \"Vendor ID:\", \"data\": \"GenuineIntel\"},"
    "      {\"field\": \"CPU family:\", \"data\": \"6\"},"
    "      {\"field\": \"Model:\", \"data\": \"61\"},"
    "      {\"field\": \"Model name:\", \"data\": \"Intel(R) Core(TM) i7-5500U CPU @ 2.40GHz\"},"
    "      {\"field\": \"Stepping:\", \"data\": \"4\"},"
    "      {\"field\": \"CPU MHz:\", \"data\": \"1448.337\"},"
    "      {\"field\": \"CPU max MHz:\", \"data\": \"3000.0000\"},"
    "      {\"field\": \"CPU min MHz:\", \"data\": \"500.0000\"},"
    "      {\"field\": \"BogoMIPS:\", \"data\": \"4788.89\"},"
    "      {\"field\": \"Virtualization:\", \"data\": \"VT-x\"},"
    "      {\"field\": \"L1d cache:\", \"data\": \"32K\"},"
    "      {\"field\": \"L1i cache:\", \"data\": \"32K\"},"
    "      {\"field\": \"L2 cache:\", \"data\": \"256K\"},"
    "      {\"field\": \"L3 cache:\", \"data\": \"4096K\"},"
    "      {\"field\": \"NUMA node0 CPU(s):\", \"data\": \"0-3\"},"
    "      {\"field\": \"Flags:\", \"data\": \"fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge mca cmov pat pse36 clflush dts acpi mmx fxsr sse sse2 ss ht tm pbe syscall nx pdpe1gb rdtscp lm constant_tsc arch_perfmon pebs bts rep_good nopl xtopology nonstop_tsc cpuid aperfmperf pni pclmulqdq dtes64 monitor ds_cpl vmx est tm2 ssse3 sdbg fma cx16 xtpr pdcm pcid sse4_1 sse4_2 x2apic movbe popcnt tsc_deadline_timer aes xsave avx f16c rdrand lahf_lm abm 3dnowprefetch cpuid_fault epb invpcid_single pti tpr_shadow vnmi flexpriority ept vpid fsgsbase tsc_adjust bmi1 avx2 smep bmi2 erms invpcid rdseed adx smap intel_pt xsaveopt ibpb ibrs stibp dtherm ida arat pln pts\"}"
    "   ]"
    "}";
static const char *XPS_13_9343_VARIANT =
    "[("
    "  'Intel(R) Core(TM) i7-5500U CPU @ 2.40GHz',"
    "  4,"
    "  3000."
    ")]";

/* Substantially trimmed output when run within VirtualBox on the machine
 * above. The main point is that "CPU max MHz:" was not present.
 */
static const char *NO_CPU_MAX_MHZ_JSON =
    "{"
    "   \"lscpu\": ["
    "      {\"field\": \"CPU(s):\", \"data\": \"1\"},"
    "      {\"field\": \"Model name:\", \"data\": \"Intel(R) Core(TM) i7-5500U CPU @ 2.40GHz\"},"
    "      {\"field\": \"CPU MHz:\", \"data\": \"2385.484\"}"
    "   ]"
    "}";
static const char *NO_CPU_MAX_MHZ_VARIANT =
    "[("
    "  'Intel(R) Core(TM) i7-5500U CPU @ 2.40GHz',"
    "  1,"
    "  2385.484"
    ")]";

static const char *ROCKCHIP_JSON =
    "{"
    "   \"lscpu\": ["
    "      {\"field\": \"Architecture:\", \"data\": \"armv7l\"},"
    "      {\"field\": \"Byte Order:\", \"data\": \"Little Endian\"},"
    "      {\"field\": \"CPU(s):\", \"data\": \"4\"},"
    "      {\"field\": \"On-line CPU(s) list:\", \"data\": \"0-3\"},"
    "      {\"field\": \"Thread(s) per core:\", \"data\": \"1\"},"
    "      {\"field\": \"Core(s) per socket:\", \"data\": \"4\"},"
    "      {\"field\": \"Socket(s):\", \"data\": \"1\"},"
    "      {\"field\": \"Vendor ID:\", \"data\": \"ARM\"},"
    "      {\"field\": \"Model:\", \"data\": \"1\"},"
    "      {\"field\": \"Model name:\", \"data\": \"Cortex-A12\"},"
    "      {\"field\": \"Stepping:\", \"data\": \"r0p1\"},"
    "      {\"field\": \"CPU max MHz:\", \"data\": \"1608.0000\"},"
    "      {\"field\": \"CPU min MHz:\", \"data\": \"126.0000\"},"
    "      {\"field\": \"BogoMIPS:\", \"data\": \"35.82\"},"
    "      {\"field\": \"Flags:\", \"data\": \"half thumb fastmult vfp edsp thumbee neon vfpv3 tls vfpv4 idiva idivt vfpd32 lpae evtstrm\"}"
    "   ]"
    "}";
static const char *ROCKCHIP_VARIANT = "[('Cortex-A12', 4,  1608.)]";

static const char *MALFORMED_JSON = "{";
static const char *WRONG_STRUCTURE_JSON_1 = "[]";
static const char *WRONG_STRUCTURE_JSON_2 = "{}";
static const char *WRONG_STRUCTURE_JSON_3 = "{\"lscpu\": true}";
static const char *WRONG_STRUCTURE_JSON_4 = "{\"lscpu\": [true]}";
static const char *WRONG_STRUCTURE_JSON_5 = "{\"lscpu\": [{}]}";
/* no field */
static const char *WRONG_STRUCTURE_JSON_6 = "{\"lscpu\": [{\"data\": \"x\"}]}";
/* no data */
static const char *WRONG_STRUCTURE_JSON_7 = "{\"lscpu\": [{\"field\": \"Model name:\"}]}";
/* field's value is not a string, or even a scalar */
static const char *WRONG_STRUCTURE_JSON_8 = "{\"lscpu\": [{\"field\": {}, \"data\": \"\"}]}";
/* data's value is not a string, or even a scalar */
static const char *WRONG_STRUCTURE_JSON_9 = "{\"lscpu\": [{\"field\": \"Model name:\", \"data\": {}}]}";

/* Well-formed, and the right shape, but the fields we expect are missing. */
static const char *MISSING_FIELDS_JSON = "{\"lscpu\": []}";
static const char *WRONG_DATA_TYPE_JSON =
  "{"
  "  \"lscpu\": ["
  "    {\"field\": \"Model name:\", \"data\": \"hello\"},"
  "    {\"field\": \"CPU(s):\", \"data\": \"3.14\"},"
  "    {\"field\": \"CPU max MHz:\", \"data\": \"extremely fast\"}"
  "  ]"
  "}";
static const char *WRONG_DATA_TYPE_VARIANT = "[('hello', 0, 0)]";

static const char *FALLBACK_VARIANT = "[('', 0, 0)]";

typedef struct _CpuTestData {
    const gchar *testpath;
    const gchar *json;
    const gchar *expected_str;
} CpuTestData;

static void
test_parse_lscpu_json (const CpuTestData *data)
{
  g_autoptr(GVariant) actual, expected;
  g_autoptr(GError) error = NULL;

  actual = eins_hwinfo_parse_lscpu_json (data->json, -1);
  g_assert_nonnull (actual);

  expected = g_variant_parse (G_VARIANT_TYPE ("a(sqd)"), data->expected_str,
                              NULL, NULL, &error);
  g_assert_no_error (error);

  if (!g_variant_equal (expected, actual))
    {
      g_autofree gchar *actual_str = g_variant_print (actual, TRUE /* type_annotate */);
      g_error ("expected %s; got %s", data->expected_str, actual_str);
    }
}

/* Just verify that we can launch lscpu, parse its output, and get something
 * other than the fallback values.
 */
static void
test_get_cpu_info_for_current_system (void)
{
  g_autoptr(GVariant) payload = eins_hwinfo_get_cpu_info ();
  const gchar *model;
  guint16 n_cpus;
  gdouble max_mhz;

  g_assert_nonnull (payload);
  g_assert_cmpstr (g_variant_get_type_string (payload), ==, "a(sqd)");
  g_assert_cmpuint (g_variant_n_children (payload), >=, 1);

  g_variant_get_child (payload, 0, "(&sqd)", &model, &n_cpus, &max_mhz);
  g_assert_cmpstr (model, !=, "");
  g_assert_cmpuint (n_cpus, >, 0);
  g_assert_cmpfloat (max_mhz, >, 0);
}

int
main (int   argc,
      char *argv[])
{
  const CpuTestData cpu_test_datas[] = {
      { "/hwinfo/cpu/bad/malformed", MALFORMED_JSON, FALLBACK_VARIANT },
      { "/hwinfo/cpu/bad/wrong-structure/1", WRONG_STRUCTURE_JSON_1, FALLBACK_VARIANT },
      { "/hwinfo/cpu/bad/wrong-structure/2", WRONG_STRUCTURE_JSON_2, FALLBACK_VARIANT },
      { "/hwinfo/cpu/bad/wrong-structure/3", WRONG_STRUCTURE_JSON_3, FALLBACK_VARIANT },
      { "/hwinfo/cpu/bad/wrong-structure/4", WRONG_STRUCTURE_JSON_4, FALLBACK_VARIANT },
      { "/hwinfo/cpu/bad/wrong-structure/5", WRONG_STRUCTURE_JSON_5, FALLBACK_VARIANT },
      { "/hwinfo/cpu/bad/wrong-structure/6", WRONG_STRUCTURE_JSON_6, FALLBACK_VARIANT },
      { "/hwinfo/cpu/bad/wrong-structure/7", WRONG_STRUCTURE_JSON_7, FALLBACK_VARIANT },
      { "/hwinfo/cpu/bad/wrong-structure/8", WRONG_STRUCTURE_JSON_8, FALLBACK_VARIANT },
      { "/hwinfo/cpu/bad/wrong-structure/9", WRONG_STRUCTURE_JSON_9, FALLBACK_VARIANT },

      { "/hwinfo/cpu/bad/missing-fields", MISSING_FIELDS_JSON, FALLBACK_VARIANT },
      { "/hwinfo/cpu/bad/wrong-data-type", WRONG_DATA_TYPE_JSON, WRONG_DATA_TYPE_VARIANT },

      { "/hwinfo/cpu/good/xps13", XPS_13_9343_JSON, XPS_13_9343_VARIANT },
      { "/hwinfo/cpu/good/no-cpu-max-mhz", NO_CPU_MAX_MHZ_JSON, NO_CPU_MAX_MHZ_VARIANT },
      { "/hwinfo/cpu/good/rockchip", ROCKCHIP_JSON, ROCKCHIP_VARIANT },
  };
  size_t i;

  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/hwinfo/disk-space/ok", test_get_disk_space_for_root);
  g_test_add_func ("/hwinfo/disk-space/noent", test_get_disk_space_for_nonexistent_dir);

  g_test_add_func ("/hwinfo/ram/current", test_get_ram_size_for_current_system);

  for (i = 0; i < G_N_ELEMENTS (cpu_test_datas); i++)
    {
      const CpuTestData *data = &cpu_test_datas[i];

      g_test_add_data_func (data->testpath,
                            data,
                            (GTestDataFunc) test_parse_lscpu_json);
    }

  g_test_add_func ("/hwinfo/cpu/current", test_get_cpu_info_for_current_system);

  return g_test_run ();
}
