OpenEmbedded/Yocto/OpenBMC BSP layer for Edgecore systems
====================================================

This layer provides support for the BMC firmware on Edgecore POWER systems server
products.

```
This layer depends on:

URI: git://git.openembedded.org/openembedded-core
layers: meta
branch: master
revision: HEAD

URI: https://github.com/openbmc/meta-phosphor
branch: master
revision: HEAD

URI: https://github.com/openbmc/meta-openpower
branch: master
revision: HEAD

URI: https://github.com/openbmc/meta-aspeed
branch: master
revision: HEAD
```

The following systems are supported.
    as9737-32db

    export TEMPLATECONF=meta-edgecore/meta-{project}
    . openbmc-env

Then build:

    bitbake obmc-phosphor-image

