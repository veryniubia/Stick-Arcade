package dev.stickarcade.player;

import android.content.Context;
import android.net.*;
import android.os.Handler;
import android.os.Looper;

final class Hotspot {
    private final ConnectivityManager manager;
    private ConnectivityManager.NetworkCallback watcher;
    private Network selected;

    Hotspot(Context context) { manager=context.getSystemService(ConnectivityManager.class); }
    void start() {
        watcher=new ConnectivityManager.NetworkCallback() {
            @Override public void onLinkPropertiesChanged(Network network, LinkProperties properties) {
                if (watcher!=this) return;
                for (LinkAddress ip : properties.getLinkAddresses()) {
                    byte[] a=ip.getAddress().getAddress();
                    if (a.length==4 && (a[0]&255)==192 && (a[1]&255)==168 && a[2]==73 && ip.getPrefixLength()==24) {
                        selected=network;
                        Arcade.network(network.getNetworkHandle());
                        return;
                    }
                }
                onLost(network);
            }
            @Override public void onLost(Network network) {
                if (watcher==this && network.equals(selected)) {
                    selected=null;
                    Arcade.network(0);
                }
            }
        };
        NetworkRequest request=new NetworkRequest.Builder().addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
                .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET).build();
        manager.registerNetworkCallback(request,watcher,new Handler(Looper.getMainLooper()));
    }
    void stop() {
        ConnectivityManager.NetworkCallback old=watcher;
        watcher=null;
        selected=null;
        if (old!=null) manager.unregisterNetworkCallback(old);
    }
}
