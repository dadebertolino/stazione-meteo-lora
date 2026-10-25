"""Attesa della sincronizzazione dell'orologio di sistema.

Il Raspberry 3 non ha un orologio a batteria: dopo un'interruzione di
corrente riparte con l'ora sbagliata finché NTP non la corregge. Fino ad
allora nessuna misura viene registrata, perché avrebbe la data sbagliata.
"""
