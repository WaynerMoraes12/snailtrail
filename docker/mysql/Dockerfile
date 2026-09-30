FROM mysql:8.4

RUN mkdir -p /var/log/mysql-slow \
    && chown mysql:mysql /var/log/mysql-slow \
    && chmod 0777 /var/log/mysql-slow

COPY docker/mysql/snailtrail.cnf /etc/mysql/conf.d/snailtrail.cnf
COPY --chmod=0755 docker/mysql/00-snailtrail.sh /docker-entrypoint-initdb.d/00-snailtrail.sh
COPY samples/shop_schema.sql /docker-entrypoint-initdb.d/10-shop-schema.sql
