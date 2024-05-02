/* Copyright 2018 Endless OS Foundation LLC. */

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

static void assert_root_disk_space (DiskSpaceType *dspace)
{
  g_assert_cmpuint (dspace->total, >, 0);
  g_assert_cmpuint (dspace->used, >, 0);
  /* maybe you have < 500 MB free, so no assertion about available itself */

  /* since we round to the nearest gigabyte, used + available <= total may not
   * hold -- what if used and available round up, but total rounds down? -- but
   * we should be within 1 GB.
   */
  g_assert_cmpuint (dspace->used + dspace->free, <=, dspace->total + 1);
}

static void
test_get_disk_space_for_root (void)
{
  g_autoptr(GFile) root = g_file_new_for_path ("/");
  DiskSpaceType dspace = {};
  g_autoptr(GError) error = NULL;
  gboolean ret;

  ret = eins_hwinfo_get_disk_space_for_partition (root, &dspace, &error);
  g_assert_true (ret);
  g_assert_no_error (error);

  assert_root_disk_space (&dspace);
}

static void
test_get_disk_space_for_nonexistent_dir (void)
{
  g_autoptr(GFile) nonexistent = NULL;
  DiskSpaceType dspace = {};
  g_autoptr(GError) error = NULL;
  gboolean ret;

  nonexistent = g_file_new_for_path ("/ca29d735-ca59-4774-8677-5bf3e9f34a7e");

  ret = eins_hwinfo_get_disk_space_for_partition (nonexistent, &dspace, &error);
  g_assert_false (ret);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);

  g_assert_cmpuint (dspace.total, ==, 0);
  g_assert_cmpuint (dspace.used, ==, 0);
  g_assert_cmpuint (dspace.free, ==, 0);
}

static void
assert_ram_size (guint32 size)
{
  /* If you have a system with less than 100 MB of RAM this test will fail.
   * Good luck running Endless OS on that!
   */
  g_assert_cmpuint (size, >=, 100);
}

static void
test_get_ram_size_for_current_system (void)
{
  guint32 size = eins_hwinfo_get_ram_size ();

  assert_ram_size (size);
}

static const char *XPS_13_9343_VARIANT =
    "[("
    "  'Intel(R) Core(TM) i7-5500U CPU @ 2.40GHz',"
    "  4,"
    "  3000.,"
    "  'fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge mca cmov pat pse36 clflush dts acpi mmx fxsr sse sse2 ss ht tm pbe syscall nx pdpe1gb rdtscp lm constant_tsc arch_perfmon pebs bts rep_good nopl xtopology nonstop_tsc cpuid aperfmperf pni pclmulqdq dtes64 monitor ds_cpl vmx est tm2 ssse3 sdbg fma cx16 xtpr pdcm pcid sse4_1 sse4_2 x2apic movbe popcnt tsc_deadline_timer aes xsave avx f16c rdrand lahf_lm abm 3dnowprefetch cpuid_fault epb invpcid_single pti tpr_shadow vnmi flexpriority ept vpid fsgsbase tsc_adjust bmi1 avx2 smep bmi2 erms invpcid rdseed adx smap intel_pt xsaveopt ibpb ibrs stibp dtherm ida arat pln pts'"
    ")]";

static const char *NO_CPU_MAX_MHZ_VARIANT =
    "[("
    "  'Intel(R) Core(TM) i7-5500U CPU @ 2.40GHz',"
    "  1,"
    "  2385.484,"
    "  'fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge mca cmov pat pse36 clflush dts acpi mmx fxsr sse sse2 ss ht tm pbe syscall nx pdpe1gb rdtscp lm constant_tsc arch_perfmon pebs bts rep_good nopl xtopology nonstop_tsc cpuid aperfmperf pni pclmulqdq dtes64 monitor ds_cpl vmx est tm2 ssse3 sdbg fma cx16 xtpr pdcm pcid sse4_1 sse4_2 x2apic movbe popcnt tsc_deadline_timer aes xsave avx f16c rdrand lahf_lm abm 3dnowprefetch cpuid_fault epb invpcid_single pti tpr_shadow vnmi flexpriority ept vpid fsgsbase tsc_adjust bmi1 avx2 smep bmi2 erms invpcid rdseed adx smap intel_pt xsaveopt ibpb ibrs stibp dtherm ida arat pln pts'"
    ")]";

static const char *ROCKCHIP_VARIANT = "[('Cortex-A12', 4,  1608., 'half thumb fastmult vfp edsp thumbee neon vfpv3 tls vfpv4 idiva idivt vfpd32 lpae evtstrm')]";
static const char *RPI4B_VARIANT = "[('Cortex-A72', 4, 1800., 'fp asimd evtstrm crc32 cpuid')]";
static const char *WRONG_DATA_TYPE_VARIANT = "[('hello', 0, 0, 'hi')]";
static const char *FALLBACK_VARIANT = "[('', 0, 0, '')]";

typedef struct _CpuTestData {
    const gchar *testpath;
    const gchar *json_resource_name;
    const gchar *expected_str;
} CpuTestData;

static void
test_parse_lscpu_json (const CpuTestData *data)
{
  g_autoptr(GVariant) actual, expected;
  g_autoptr(GError) error = NULL;
  g_autoptr(GBytes) bytes = NULL;
  g_autofree gchar *resource_path = NULL;
  const char *json = NULL;
  gsize size = -1;

  resource_path = g_strjoin ("/",
                             "/com/endlessm/MetricsInstrumentation/cpuinfo",
                             data->json_resource_name,
                             NULL);
  bytes = g_resources_lookup_data (resource_path,
                                   G_RESOURCE_LOOKUP_FLAGS_NONE,
                                   &error);
  g_assert_no_error (error);
  json = g_bytes_get_data (bytes, &size);

  actual = eins_hwinfo_parse_lscpu_json (json, size);
  g_assert_nonnull (actual);

  expected = g_variant_parse (G_VARIANT_TYPE ("a(sqds)"), data->expected_str,
                              NULL, NULL, &error);
  g_assert_no_error (error);

  if (!g_variant_equal (expected, actual))
    {
      g_autofree gchar *actual_str = g_variant_print (actual, TRUE /* type_annotate */);
      g_error ("expected %s; got %s", data->expected_str, actual_str);
    }
}

static void
assert_cpu_info_for_current_system (GVariant *cpu_payload)
{
  const gchar *model;
  guint16 n_cpus;
  gdouble max_mhz;
  const gchar *flags;

  g_assert_nonnull (cpu_payload);
  g_assert_cmpstr (g_variant_get_type_string (cpu_payload), ==, "a(sqds)");
  g_assert_cmpuint (g_variant_n_children (cpu_payload), >=, 1);

  g_variant_get_child (cpu_payload, 0, "(&sqds)", &model, &n_cpus, &max_mhz, &flags);
  g_assert_cmpstr (model, !=, "");
  g_assert_cmpuint (n_cpus, >, 0);
  g_assert_cmpfloat (max_mhz, >=, 0);
  g_assert_cmpstr (flags, !=, "");
}

/* Just verify that we can launch lscpu, parse its output, and get something
 * other than the fallback values.
 */
static void
test_get_cpu_info_for_current_system (void)
{
  g_autoptr(GVariant) payload = eins_hwinfo_get_cpu_info ();

  assert_cpu_info_for_current_system (payload);
}

static void
test_get_computer_hwinfo (void)
{
  g_autoptr(GVariant) payload = eins_hwinfo_get_computer_hwinfo ();
  guint32 ram_size;
  DiskSpaceType dspace;
  g_autoptr(GVariant) cpu_payload;

  g_assert_nonnull (payload);
  g_assert_cmpstr (g_variant_get_type_string (payload), ==, "(uuuua(sqds))");

  g_variant_get (payload, "(uuuu@a(sqds))", &ram_size,
                 &dspace.total, &dspace.used, &dspace.free, &cpu_payload);

  assert_ram_size (ram_size);
  assert_root_disk_space (&dspace);
  assert_cpu_info_for_current_system (cpu_payload);
}

int
main (int   argc,
      char *argv[])
{
  const CpuTestData cpu_test_datas[] = {
      { "/hwinfo/cpu/bad/malformed", "bad-malformed.json", FALLBACK_VARIANT },
      { "/hwinfo/cpu/bad/wrong-structure/1", "bad-wrong-structure-1.json", FALLBACK_VARIANT },
      { "/hwinfo/cpu/bad/wrong-structure/2", "bad-wrong-structure-2.json", FALLBACK_VARIANT },
      { "/hwinfo/cpu/bad/wrong-structure/3", "bad-wrong-structure-3.json", FALLBACK_VARIANT },
      { "/hwinfo/cpu/bad/wrong-structure/4", "bad-wrong-structure-4.json", FALLBACK_VARIANT },
      { "/hwinfo/cpu/bad/wrong-structure/5", "bad-wrong-structure-5.json", FALLBACK_VARIANT },
      /* no field */
      { "/hwinfo/cpu/bad/wrong-structure/6", "bad-wrong-structure-6.json", FALLBACK_VARIANT },
      /* no data */
      { "/hwinfo/cpu/bad/wrong-structure/7", "bad-wrong-structure-7.json", FALLBACK_VARIANT },
      /* field's value is not a string, or even a scalar */
      { "/hwinfo/cpu/bad/wrong-structure/8", "bad-wrong-structure-8.json", FALLBACK_VARIANT },
      /* data's value is not a string, or even a scalar */
      { "/hwinfo/cpu/bad/wrong-structure/9", "bad-wrong-structure-9.json", FALLBACK_VARIANT },

      /* Well-formed, and the right shape, but the fields we expect are missing. */
      { "/hwinfo/cpu/bad/missing-fields", "bad-missing-fields.json", FALLBACK_VARIANT },

      { "/hwinfo/cpu/bad/wrong-data-type", "bad-wrong-data-type.json", WRONG_DATA_TYPE_VARIANT },

      { "/hwinfo/cpu/good/xps13", "good-xps13.json", XPS_13_9343_VARIANT },
      /* Substantially trimmed output when run within VirtualBox on the machine
       * above. The main point is that "CPU max MHz:" was not present.
       */
      { "/hwinfo/cpu/good/no-cpu-max-mhz", "good-no-cpu-max-mhz.json", NO_CPU_MAX_MHZ_VARIANT },
      { "/hwinfo/cpu/good/rockchip", "good-rockchip.json", ROCKCHIP_VARIANT },
      { "/hwinfo/cpu/good/rpi4b", "good-rpi4b.json", RPI4B_VARIANT },
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

  g_test_add_func ("/hwinfo/computer/current", test_get_computer_hwinfo);

  return g_test_run ();
}
