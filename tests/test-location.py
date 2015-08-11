#!/usr/bin/env python3

# Copyright (c) 2015 Endless Mobile, Inc.

# This file is part of eos-metrics-instrumentation.
#
# eos-metrics-instrumentation is free software: you can redistribute it and/or
# modify it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or (at your
# option) any later version.
#
# eos-metrics-instrumentation is distributed in the hope that it will be
# useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
# Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with eos-metrics-instrumentation.  If not, see
# <http://www.gnu.org/licenses/>.

import dbus
import dbus.mainloop.glib
import os
import subprocess
import tempfile
import unittest
import uuid

import dbusmock
from gi.repository import GLib

GEOCLUE_IFACE = 'org.freedesktop.GeoClue2'
GEOCLUE_OBJECT = GEOCLUE_IFACE
GEOCLUE_PATH = '/org/freedesktop/GeoClue2'
GEOCLUE_MANAGER_IFACE = 'org.freedesktop.GeoClue2.Manager'
GEOCLUE_MANAGER_PATH = '/org/freedesktop/GeoClue2/Manager'
GEOCLUE_CLIENT_IFACE = 'org.freedesktop.GeoClue2.Client'
GEOCLUE_CLIENT_PATH = '/org/freedesktop/GeoClue2/Client0'
GEOCLUE_LOCATION_IFACE = 'org.freedesktop.GeoClue2.Location'
GEOCLUE_LOCATION_PATH = '/org/freedesktop/GeoClue2/Location0'
METRICS_OBJECT = 'com.endlessm.Metrics'
METRICS_IFACE = 'com.endlessm.Metrics.EventRecorderServer'
METRICS_PATH = '/com/endlessm/Metrics'
MOCK_LATITUDE = 49.2848
MOCK_LONGITUDE = -123.1228
MOCK_ACCURACY = 50000.0
MOCK_ALTITUDE = 31.0
UNKNOWN_ALTITUDE = -1.797693e+308  # minimum double value = unknown
CITY_LEVEL_ACCURACY = 4  # from GeoClue2 spec
USER_LOCATION_EVENT = uuid.UUID('abe7af92-6704-4d34-93cf-8f1b46eb09b8')

class TestLocationIntegration(dbusmock.DBusTestCase):
    """
    Ensures that the instrumentation daemon asks for a location upon startup,
    and that it logs an event with the location.
    """

    @classmethod
    def setUpClass(klass):
        # Prepare mock DBus for integration with GLib main loop
        dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)

        klass.start_system_bus()
        klass.dbus_con = klass.get_dbus(system_bus=True)

        # Populate it with services accessed from the daemon that we are not
        # testing in this test suite.
        # These will be terminated in tearDownClass() automatically.
        klass.spawn_server_template('networkmanager')
        klass.spawn_server_template('logind')
        (polkit_popen, polkit_obj) = klass.spawn_server_template('polkitd')
        polkit_obj.AllowUnknown(True)

        # Don't write to a persistent tally file in /var. That will cause a
        # warning on normal test run. It will error out on distcheck, because
        # /var is actually writable and then installcheck will fail because the
        # file isn't removed by uninstall.
        klass.cache = tempfile.NamedTemporaryFile(delete=False)
        klass.cache.close()
        os.environ['EOS_INSTRUMENTATION_CACHE'] = klass.cache.name

    @classmethod
    def tearDownClass(klass):
        super(TestLocationIntegration, klass).tearDownClass()
        os.unlink(klass.cache.name)

    def setUp(self):
        # Put geoclue mocks onto the mock system bus
        self.geoclue_popen = self.spawn_server(GEOCLUE_OBJECT, GEOCLUE_PATH,
            GEOCLUE_IFACE, system_bus=True)
        self.geoclue_mock = dbus.Interface(self.dbus_con.get_object(
            GEOCLUE_OBJECT, GEOCLUE_PATH), dbusmock.MOCK_IFACE)
        self.geoclue_mock.AddObject(GEOCLUE_MANAGER_PATH, GEOCLUE_MANAGER_IFACE,
            {}, [
                ('GetClient', '', 'o', 'ret = ' + repr(GEOCLUE_CLIENT_PATH)),
            ])
        self.geoclue_mock.AddObject(GEOCLUE_CLIENT_PATH, GEOCLUE_CLIENT_IFACE,
            {
                'DesktopId': dbus.String(''),
                'DistanceThreshold': dbus.UInt32(0),
                'RequestedAccuracyLevel': dbus.UInt32(0),
            }, [
                ('Start', '', '', ''),
            ])
        self.geoclue_mock.AddObject(GEOCLUE_LOCATION_PATH,
            GEOCLUE_LOCATION_IFACE, {
                'Latitude': MOCK_LATITUDE,
                'Longitude': MOCK_LONGITUDE,
                'Accuracy': MOCK_ACCURACY,
                'Altitude': MOCK_ALTITUDE,
                'Description': dbus.String(''),
            }, [])

        self.geoclue_client = self.dbus_con.get_object(GEOCLUE_IFACE,
            GEOCLUE_CLIENT_PATH)
        self.geoclue_location = self.dbus_con.get_object(GEOCLUE_IFACE,
            GEOCLUE_LOCATION_PATH)

        self.metrics_popen = self.spawn_server(METRICS_OBJECT, METRICS_PATH,
            METRICS_IFACE, system_bus=True)
        self.metrics_mock = dbus.Interface(self.dbus_con.get_object(
            METRICS_OBJECT, METRICS_PATH), dbusmock.MOCK_IFACE)
        self.metrics_mock.AddMethod('', 'RecordSingularEvent', 'uayxbv', '', '')

        # Mechanism for blocking on a particular call
        self.dbus_con.add_signal_receiver(self.handle_dbus_event_received,
                                          signal_name='MethodCalled',
                                          dbus_interface=dbusmock.MOCK_IFACE)
        self.mainloop = GLib.MainLoop()
        self._quit_on_method = ''

        self.daemon = subprocess.Popen('./eos-metrics-instrumentation')

    def tearDown(self):
        self.dbus_con.remove_signal_receiver(self.handle_dbus_event_received,
                                             signal_name='MethodCalled')

        # Terminate child first so it doesn't try to spawn any real services
        # after the fake ones have shut down.
        self.daemon.terminate()
        self.daemon.wait()

        self.geoclue_popen.terminate()
        self.metrics_popen.terminate()
        self.geoclue_popen.wait()
        self.metrics_popen.wait()

    def quit_on(self, method_name):
        """Quit the main loop when the DBus method @method_name is called.
        Timeout after waiting for 20 seconds. Use like this:
            self.quit_on('MyMethod')
            self.mainloop.run()
            # now MyMethod has been called
        """
        self._quit_on_method = method_name
        GLib.timeout_add_seconds(20, self.fail, 'Test timed out after ' +
                                 'waiting 20 seconds for D-Bus method call.')

    def handle_dbus_event_received(self, name, *args):
        if name == self._quit_on_method:
            self.mainloop.quit()
            self._quit_on_method = ''

    def test_daemon_identifies_itself_to_geoclue(self):
        self.quit_on('Start')
        self.mainloop.run()

        desktop_id = self.geoclue_client.Get('', 'DesktopId')
        self.assertEquals('eos-metrics-instrumentation', desktop_id)

    def test_daemon_does_not_ask_for_too_much_accuracy(self):
        self.quit_on('Start')
        self.mainloop.run()

        accuracy = self.geoclue_client.Get('', 'RequestedAccuracyLevel')
        self.assertEquals(CITY_LEVEL_ACCURACY, accuracy)

    def dbus_bytes_to_uuid(self, dbus_byte_array):
        bytes_as_bytes = bytes(dbus_byte_array)
        return uuid.UUID(bytes=bytes_as_bytes)

    def test_daemon_sends_event(self):
        self.quit_on('Start')
        self.mainloop.run()

        self.geoclue_client.EmitSignal('', 'LocationUpdated', 'oo',
            [GEOCLUE_LOCATION_PATH, GEOCLUE_LOCATION_PATH])
        self.quit_on('RecordSingularEvent')
        self.mainloop.run()

        calls = self.metrics_mock.GetCalls()
        args = calls[0][2]
        event_id_as_dbus_bytes = args[1]
        event_id = self.dbus_bytes_to_uuid(event_id_as_dbus_bytes)
        payload = args[4]

        self.assertEquals(event_id, USER_LOCATION_EVENT)

        self.assertEquals(payload[0], MOCK_LATITUDE)
        self.assertEquals(payload[1], MOCK_LONGITUDE)
        self.assertEquals(payload[2], True)
        self.assertEquals(payload[3], MOCK_ALTITUDE)
        self.assertEquals(payload[4], MOCK_ACCURACY)

    def test_daemon_recognizes_when_it_cant_determine_altitude(self):
        self.quit_on('Start')
        self.mainloop.run()

        self.geoclue_location.Set('org.freedesktop.GeoClue2.Location',
            'Altitude', UNKNOWN_ALTITUDE)
        self.geoclue_client.EmitSignal('', 'LocationUpdated', 'oo',
            [GEOCLUE_LOCATION_PATH, GEOCLUE_LOCATION_PATH])
        self.quit_on('RecordSingularEvent')
        self.mainloop.run()

        payload = self.metrics_mock.GetCalls()[0][2][4]

        self.assertEquals(payload[2], False)

if __name__ == '__main__':
    unittest.main()
