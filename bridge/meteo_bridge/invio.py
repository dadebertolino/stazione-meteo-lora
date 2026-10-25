"""Invio delle misure al plugin WordPress.

POST https://<sito>/wp-json/dbmt/v1/misure, in gruppi, con il token nell'header
Authorization: Bearer. Le misure vengono segnate come inviate solo dopo la
risposta positiva del sito; i reinvii sono innocui perché il plugin scarta
le misure già presenti.
"""
