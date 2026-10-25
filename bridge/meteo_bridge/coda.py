"""Coda locale delle misure, in SQLite.

Ogni misura entra con la sua ora di arrivo e resta "da inviare" finché il
sito non conferma. Senza internet o con il sito fermo non si perde niente:
le misure partono più tardi, con l'ora originale.
"""
