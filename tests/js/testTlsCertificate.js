const MainLoop = imports.mainloop;
const Glib     = imports.gi.GLib;
const Evd      = imports.gi.Evd;

const Assert   = imports.common.assert;
const Test     = imports.common.test;

function testInitialState (Assert) {
    let cert = new Evd.TlsCertificate ();
    Assert.ok (cert);

    Assert.equal (cert.type, Evd.TlsCertificateType.UNKNOWN);

    // when in uninitialized state, all property getters return error
    let dn = null;
    try {
        dn = cert.get_dn ();
    }
    catch (e) {
        Assert.ok (e);
    }
    Assert.equal (dn, null);

    let time = -1;
    try {
        time = cert.get_activation_time ();
    }
    catch (e) {
        Assert.ok (e);
    }
    Assert.equal (time, -1);

    try {
        time = cert.get_expiration_time ();
    }
    catch (e) {
        Assert.ok (e);
    }
    Assert.equal (time, -1);
}

function testX509Import (Assert) {
    let cert = new Evd.TlsCertificate ();

    let [result, rawPem] = Glib.file_get_contents ("certs/x509-server.pem");

    Assert.ok (cert.import (rawPem, rawPem.length));
    Assert.equal (cert.get_dn (), "O=EventDance,CN=eventdance.org");
    Assert.equal (cert.type, Evd.TlsCertificateType.X509);
    Assert.equal (cert.get_expiration_time ().toString (), "Wed Mar 23 2011 15:43:49 GMT+0100 (CET)");
    Assert.equal (cert.get_activation_time ().toString (), "Tue Mar 23 2010 15:43:49 GMT+0100 (CET)");
    Assert.equal (cert.verify_validity (), Evd.TlsVerifyState.OK);

    let [result, rawPem] = Glib.file_get_contents ("certs/x509-jane.pem");
    Assert.ok (cert.import (rawPem, rawPem.length));
    Assert.equal (cert.get_dn (), "CN=Jane");
    Assert.equal (cert.type, Evd.TlsCertificateType.X509);
    Assert.equal (cert.get_expiration_time ().toString (), "Wed Mar 23 2011 15:48:25 GMT+0100 (CET)");
    Assert.equal (cert.get_activation_time ().toString (), "Tue Mar 23 2010 15:48:25 GMT+0100 (CET)");
    Assert.equal (cert.verify_validity (), Evd.TlsVerifyState.OK);
}

Evd.tls_init ();

Test.run (this);

Evd.tls_deinit ();
