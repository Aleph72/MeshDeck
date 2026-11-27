# MeshDeck

MeshDeck è un firmware per usare **Meshtastic direttamente dal LILYGO T-Deck Plus**, con un’interfaccia pensata per lo schermo, la tastiera e la trackball del dispositivo. Il progetto nasce da [T-UI](https://github.com/jeeab/t-ui-firmware) e conserva la compatibilità radio con i nodi [Meshtastic](https://meshtastic.org/) configurati sulla stessa rete.

## Cosa include

- **Home personalizzabile** con scorciatoie alle chat, conteggio dei non letti e icone spostabili tra pagine.
- **Mappe offline** con riquadri salvati su microSD, elenco dei ripetitori HearHam e calcolatrice.
- **Meteo** con ricerca per città, previsioni a cinque giorni e ultimi dati salvati.
- **Impostazioni e diagnostica** con suonerie, volume, blocco tastiera e dati sulla memoria.
- **Italiano, spagnolo e inglese** per la Home, le preferite, il riordino, il menu principale delle impostazioni e il meteo. Le traduzioni delle altre schermate proseguono.

## Hardware e installazione

Il dispositivo collaudato è il **LILYGO T-Deck Plus con LoRa, ESP32-S3 e 16 MB di flash**. Il T-Deck base è da verificare. Il T-Deck Pro con schermo e-ink richiede un porting dedicato.

La versione **2026.11.26.6** è stata provata sul T-Deck Plus. Il flasher web e il canale pubblico degli aggiornamenti sono in preparazione. L’immagine per aggiornare un dispositivo già predisposto e il pacchetto per la prima installazione hanno procedure distinte.

## Compilazione e contributi

La build usa Python 3.10+ e PlatformIO con il target `t-deck-tft`. Per la procedura completa, le dipendenze e le verifiche, consulta la [documentazione tecnica in inglese](README.field.md).

Segnalazioni e contributi sono benvenuti. In caso di problema indica il modello del dispositivo, la versione di MeshDeck e i passaggi per riprodurlo.

## Licenza e crediti

MeshDeck è un progetto indipendente basato su T-UI e Meshtastic. I crediti originali sono conservati nel [README di T-UI](README.upstream.md). Il codice è distribuito secondo la [licenza GPL-3.0](LICENSE).
