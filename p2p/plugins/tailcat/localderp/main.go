// Command localderp runs a throwaway DERP relay on 127.0.0.1 and serves a
// matching DERP map, so the tailcat plugin can be tested without reaching
// Tailscale's public relays:
//
//	go run ./localderp &   # prints "DERPMAP http://127.0.0.1:<port>/derpmap.json"
//	R1P_CONFIG='{"relay_url":"<that url>"}' ../test/run_loopback.sh r1delta_tailcat.so
package main

import (
	"crypto/tls"
	"encoding/json"
	"fmt"
	"log"
	"net"
	"net/http"
	"net/http/httptest"

	"tailscale.com/derp/derpserver"
	"tailscale.com/tailcfg"
	"tailscale.com/types/key"
)

func main() {
	d := derpserver.New(key.NewNode(), log.Printf)
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		log.Fatal(err)
	}
	srv := httptest.NewUnstartedServer(derpserver.Handler(d))
	srv.Listener.Close()
	srv.Listener = ln
	srv.Config.TLSNextProto = make(map[string]func(*http.Server, *tls.Conn, http.Handler))
	srv.StartTLS()

	dm := &tailcfg.DERPMap{Regions: map[tailcfg.DERPRegionID]*tailcfg.DERPRegion{
		1: {RegionID: 1, RegionCode: "local", RegionName: "Local test", Nodes: []*tailcfg.DERPNode{{
			Name: "l1", RegionID: 1, HostName: "127.0.0.1", IPv4: "127.0.0.1", IPv6: "none",
			STUNPort: -1, DERPPort: ln.Addr().(*net.TCPAddr).Port, InsecureForTests: true,
		}}},
	}}
	body, _ := json.Marshal(dm)
	mapLn, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		log.Fatal(err)
	}
	fmt.Printf("DERPMAP http://%s/derpmap.json\n", mapLn.Addr())
	log.Fatal(http.Serve(mapLn, http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		w.Write(body)
	})))
}
