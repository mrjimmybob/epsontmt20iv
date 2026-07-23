# Impresora de tickets — resolución de problemas

Impresora térmica de tickets **Epson TM-T20IV**, a la que se imprime a través de
la cola de CUPS `TMT20IV-ttp` del servidor de impresión.

Este documento tiene dos partes: comprobaciones que **puede hacer cualquiera** en
la tienda (sin comandos), y una secuencia de diagnóstico **para el técnico**.

*(English version: TROUBLESHOOTING.md)*

---

# Parte 1 — Comprobaciones sin comandos

## No sale nada por la impresora

Siga estos pasos en orden; los tres primeros resuelven la mayoría de los casos:

1. **¿Hay papel?** Abra la tapa y compruebe que el rollo no esté vacío ni a punto
   de acabarse. Reponga el rollo y **cierre la tapa con firmeza hasta que haga
   clic.**
2. **¿Está bien cerrada la tapa?** Una tapa que no ha quedado bien encajada
   detiene la impresión igual que un rollo vacío.
3. **¿Está encendida la impresora?** Compruebe el piloto de encendido. Si hay una
   luz parpadeando en rojo o naranja, fíjese en cómo parpadea: al técnico le
   servirá.
4. **¿Está conectado el cable de red** en la parte trasera de la impresora, y
   encendida la lucecita que hay junto al conector?

**Después de reponer papel, espere aproximadamente un minuto.** El ticket
pendiente debería salir solo: el sistema reintenta automáticamente y no hace
falta volver a mandarlo a imprimir. Si pasados un par de minutos no sale nada,
avise al técnico.

> No pulse «Imprimir» una y otra vez. Los intentos repetidos se acumulan en la
> cola y saldrán todos de golpe en cuanto se resuelva el problema.

## Sale algo, pero mal

| Lo que ve | Qué significa |
|---|---|
| El ticket sale **diminuto**: un bloque pequeño de texto en un papel ancho | Problema de configuración. Avise al técnico (se corrige con una sola línea). |
| Sale **papel en blanco** y corta | El contenido del ticket no está llegando. Avise al técnico. |
| El ticket sale **cortado por la mitad**, o con varios cortes | Avise al técnico. |
| El texto se lee, pero **muy tenue** | Normalmente el rollo está colocado del revés, o es papel de mala calidad. Pruebe a recolocarlo. |

## Apagar y encender

Si la impresora parece completamente bloqueada —no imprime nada y reponer papel
no soluciona nada—, **apáguela, espere 5 segundos y vuelva a encenderla.**
Después imprima un ticket de prueba. Es una operación segura y resuelve los
bloqueos reales de la impresora.

---

# Parte 2 — Para el técnico

## La prueba que parte el problema en dos

Ejecútela en el servidor de impresión, sustituyendo la IP. Habla
**directamente con la impresora**, sin pasar por CUPS:

```bash
curl -k -X POST "https://<ip-impresora>/cgi-bin/epos/service.cgi?devid=local_printer&timeout=10000" \
  -H "Content-Type: text/xml; charset=utf-8" -H 'SOAPAction: ""' \
  --data-binary '<?xml version="1.0" encoding="utf-8"?><s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"><s:Body><epos-print xmlns="http://www.epson-pos.com/schemas/2011/03/epos-print"><text align="center">test ok</text><feed line="3"/><cut type="feed"/></epos-print></s:Body></s:Envelope>'
```

- **Imprime y la respuesta contiene `success="true"`** → la impresora y la red
  están bien; el problema está en el lado de CUPS.
- **La respuesta contiene `EPTR_REC_EMPTY`** → sin papel.
- **La respuesta contiene `EX_TIMEOUT` de forma repetida** → el mecanismo de
  impresión está bloqueado; apague y encienda la impresora.
- **`curl` no llega a conectar** → problema de red o de IP (vea «La IP de la
  impresora ha cambiado»).

## Comandos de diagnóstico

```bash
lpstat -p TMT20IV-ttp -l     # estado de la cola y el motivo (sin papel, deshabilitada, ...)
lpstat -o                    # trabajos atascados en la cola
lpstat -v                    # confirma que la cola apunta a epos://<ip-impresora>
```

Para más detalle, suba el nivel de registro, reimprima y vuelva a bajarlo:

```bash
sudo cupsctl LogLevel=debug
lp -d TMT20IV-ttp /tmp/sample_receipt_80mm.pdf
grep -E 'rastertotmt20iv|epos:|STATE:' /var/log/cups/error_log | tail -30
sudo cupsctl LogLevel=warn
```

Los mensajes del filtro y del backend van a **`/var/log/cups/error_log`**, no a
`journalctl`: `journalctl -u cups` solo muestra el demonio en sí.

## Casos habituales

| Síntoma | Causa y solución |
|---|---|
| `lpstat` indica **falta de papel** | Reponga papel; el trabajo retenido se reintenta solo. Si ha pasado más tiempo que la ventana de reintentos (vea SERVER-INSTALL.md), vuelva a enviarlo. |
| La cola aparece **deshabilitada** | `sudo cupsenable TMT20IV-ttp` (y `sudo cupsaccept TMT20IV-ttp`). |
| **Trabajos acumulados** y no imprime nada | Diagnostique primero y después vacíe la cola con `cancel -a TMT20IV-ttp`. |
| El ticket sale al **~25 % de su tamaño** | `print-scaling` no está en `none`: `sudo lpadmin -p TMT20IV-ttp -o print-scaling-default=none`. No elimine nunca este ajuste. |
| El registro muestra `page width … != expected 576` | La página de origen no mide 80 mm de ancho: es un problema de tamaño de página o CSS en la aplicación que envía el ticket, no del controlador. |
| La prueba con `curl` funciona, pero CUPS no imprime | Compruebe que el controlador está instalado (`dpkg -l epsontmt20iv`) y que la URI de la cola es correcta (`lpstat -v`). |
| La prueba con `curl` no conecta | La IP de la impresora ha cambiado, o hay un problema de red (vea más abajo). |
| La impresora responde pero no imprime, con `status="1"` / `EX_TIMEOUT` persistente | Mecanismo bloqueado. **Apague y encienda la impresora.** |

## La IP de la impresora ha cambiado

Apunte la cola existente a la nueva dirección; no hace falta reinstalar nada:

```bash
sudo lpadmin -p TMT20IV-ttp -v epos://<nueva-ip-impresora>
```

Después fíjela (IP estática o reserva DHCP en el router) para que no vuelva a
cambiar.

## Los PC cliente no imprimen, pero el servidor sí

Los equipos cliente no llevan el controlador: imprimen contra la **cola
compartida** del servidor. Compruebe que el servidor es accesible y que el uso
compartido está activado (`sudo cupsctl _share_printers=1`), y vuelva a añadir la
cola en el cliente (CLIENT-INSTALL.md).

## Información de fondo

El detalle técnico —por qué esta impresora necesita un controlador propio, el
protocolo ePOS, los códigos de estado verificados— está en **FACTS.md**.
